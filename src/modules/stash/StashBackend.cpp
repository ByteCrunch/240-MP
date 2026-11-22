#include "StashBackend.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSslError>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace {

// Self-signed / LAN certificates are common for a Stash server, so relax only
// the errors a local install typically produces, and only for the configured
// host (same policy as JellyfinBackend / EmbyBackend).
QList<QSslError> filterExpectedSslErrors(const QList<QSslError> &errors) {
    static const QSet<QSslError::SslError> kExpected = {
        QSslError::SelfSignedCertificate,
        QSslError::HostNameMismatch,
        QSslError::UnableToGetLocalIssuerCertificate,
        QSslError::UnableToVerifyFirstCertificate,
    };
    QList<QSslError> allowed;
    for (const QSslError &e : errors) {
        if (kExpected.contains(e.error()))
            allowed.append(e);
    }
    return allowed;
}

// find_filter / object_filter exist since Stash v0.23 (current stable is
// v0.32), which is what lets a saved filter be replayed verbatim.
const QString kSavedFiltersQuery = QStringLiteral(R"GQL(
query FindSavedFilters($mode: FilterMode) {
  findSavedFilters(mode: $mode) {
    id
    name
    find_filter { q page per_page sort direction }
    object_filter
  }
}
)GQL");

const QString kTagsQuery = QStringLiteral(R"GQL(
query FindTags($filter: FindFilterType) {
  findTags(filter: $filter) {
    tags { id name }
  }
}
)GQL");

const QString kScenesQuery = QStringLiteral(R"GQL(
query FindScenes($filter: FindFilterType, $sceneFilter: SceneFilterType) {
  findScenes(filter: $filter, scene_filter: $sceneFilter) {
    scenes {
      id
      title
      files { basename duration }
      paths { stream }
      tags { id name }
    }
  }
}
)GQL");

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

StashBackend::StashBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
    , m_nam(new QNetworkAccessManager(this))
{
    loadConfig();
}

// ---------------------------------------------------------------------------
// Config (stash.json)
// ---------------------------------------------------------------------------

QString StashBackend::configPath() const {
    // Read-only by design: the API key never goes into config.json.
    return m_dataRoot + "/stash.json";
}

QString StashBackend::normalizeServerUrl(const QString &url) {
    QString u = url.trimmed();
    while (u.endsWith('/'))
        u.chop(1);
    // Tolerate a pasted GraphQL endpoint — the module appends "/graphql" itself.
    if (u.endsWith(QLatin1String("/graphql"), Qt::CaseInsensitive))
        u.chop(8);
    while (u.endsWith('/'))
        u.chop(1);
    return u;
}

void StashBackend::loadConfig() {
    m_serverUrl.clear();
    m_apiKey.clear();
    m_configError.clear();

    auto fail = [this](const QString &message) {
        m_configError = message;
        qWarning("[StashBackend] %s: %s", qPrintable(message), qPrintable(configPath()));
    };

    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("STASH.JSON NOT FOUND"));
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(QStringLiteral("INVALID JSON IN STASH.JSON"));
        return;
    }

    const QJsonObject cfg = doc.object();
    const QString rawUrl = cfg["serverUrl"].toString().trimmed();
    if (rawUrl.isEmpty()) {
        fail(QStringLiteral("SERVERURL MISSING IN STASH.JSON"));
        return;
    }

    const QString normalized = normalizeServerUrl(rawUrl);
    if (!normalized.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !normalized.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        fail(QStringLiteral("SERVERURL MUST START WITH HTTP:// OR HTTPS://"));
        return;
    }

    m_serverUrl = normalized;
    m_apiKey = cfg["apiKey"].toString().trimmed();
}

bool StashBackend::has_config() const {
    return !m_serverUrl.isEmpty();
}

QString StashBackend::get_config_path() const {
    return configPath();
}

QString StashBackend::get_config_error() const {
    if (has_config())
        return {};
    return m_configError.isEmpty() ? QStringLiteral("STASH.JSON NOT CONFIGURED") : m_configError;
}

void StashBackend::reload_config() {
    loadConfig();
}

// ---------------------------------------------------------------------------
// HTTP / GraphQL helpers
// ---------------------------------------------------------------------------

void StashBackend::ignoreSslErrors(QNetworkReply *reply) const {
    connect(reply, &QNetworkReply::sslErrors, reply, [this, reply](const QList<QSslError> &errors) {
        if (m_serverUrl.isEmpty())
            return;
        if (reply->url().host() != QUrl(m_serverUrl).host())
            return;
        const QList<QSslError> allowed = filterExpectedSslErrors(errors);
        if (!allowed.isEmpty())
            reply->ignoreSslErrors(allowed);
    });
}

void StashBackend::postGraphql(const QString &query, const QJsonObject &variables,
                               const QString &errorPrefix,
                               std::function<void(const QJsonObject &)> onData) {
    if (!has_config()) {
        emit errorOccurred(get_config_error());
        return;
    }

    QJsonObject payload;
    payload["query"] = query;
    payload["variables"] = variables;

    const QUrl endpoint(m_serverUrl + "/graphql");
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    // Never follow a redirect: the ApiKey header must not be replayed to a
    // different host (and a redirect here almost always means a bad serverUrl).
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    if (!m_apiKey.isEmpty())
        req.setRawHeader("ApiKey", m_apiKey.toUtf8());

    auto *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    ignoreSslErrors(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, errorPrefix, onData]() {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (status == 401 || status == 403)
                emit errorOccurred(QStringLiteral("AUTH FAILED - CHECK APIKEY IN STASH.JSON"));
            else
                emit errorOccurred(errorPrefix + ": " + reply->errorString());
            return;
        }

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred(QStringLiteral("INVALID RESPONSE FROM SERVER - CHECK SERVERURL"));
            return;
        }

        const QJsonObject root = doc.object();
        const QJsonArray errors = root["errors"].toArray();
        if (!errors.isEmpty()) {
            QString message = errors.first().toObject()["message"].toString().trimmed();
            if (message.isEmpty())
                message = QStringLiteral("UNKNOWN GRAPHQL ERROR");
            emit errorOccurred(errorPrefix + ": " + message);
            return;
        }

        onData(root["data"].toObject());
    });
}

// ---------------------------------------------------------------------------
// Scene formatting
// ---------------------------------------------------------------------------

QJsonObject StashBackend::sceneFindFilter(const QVariantMap &savedFindFilter) {
    QJsonObject filter;

    // A saved filter carries the query and sort the user set up in Stash; keep
    // them as-is so a menu row resolves to the same list the Stash UI shows.
    const QString q = savedFindFilter.value("q").toString().trimmed();
    if (!q.isEmpty())
        filter["q"] = q;

    const QString sort = savedFindFilter.value("sort").toString().trimmed();
    if (!sort.isEmpty())
        filter["sort"] = sort;

    const QString direction = savedFindFilter.value("direction").toString().trimmed();
    if (direction == QLatin1String("ASC") || direction == QLatin1String("DESC"))
        filter["direction"] = direction;

    // Always request the whole list (matches the Plex/Jellyfin modules' unbounded
    // browse lists); "per_page = -1" means "all results" in Stash.
    filter["page"] = 1;
    filter["per_page"] = -1;
    return filter;
}

QVariantList StashBackend::buildFilterRows(const QJsonArray &savedFilters) {
    QVariantList saved;
    for (const QJsonValue &v : savedFilters) {
        const QJsonObject o = v.toObject();
        const QString name = o["name"].toString().trimmed();
        if (name.isEmpty())
            continue;
        saved.append(QVariantMap{
            {"id", o["id"].toString()},
            {"title", name},
            {"findFilter", o["find_filter"].toObject().toVariantMap()},
            {"objectFilter", o["object_filter"].toObject().toVariantMap()},
        });
    }

    std::sort(saved.begin(), saved.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("title").toString().compare(
                   b.toMap().value("title").toString(), Qt::CaseInsensitive) < 0;
    });

    // "ALL SCENES" is always first so the module has a stable entry point even
    // when the user has not saved any filters yet.
    QVariantList rows;
    rows.append(QVariantMap{
        {"id", QStringLiteral("all")},
        {"title", QStringLiteral("ALL SCENES")},
        {"findFilter", QVariantMap{}},
        {"objectFilter", QVariantMap{}},
    });
    rows.append(saved);
    return rows;
}

QJsonObject StashBackend::sceneFilterForTag(const QString &tagId) {
    QJsonObject tagCriterion;
    tagCriterion["value"] = QJsonArray{tagId};
    // INCLUDES with no depth: only this tag matches, not its descendants.
    tagCriterion["modifier"] = QStringLiteral("INCLUDES");

    QJsonObject sceneFilter;
    sceneFilter["tags"] = tagCriterion;
    return sceneFilter;
}

QVariantMap StashBackend::formatScene(const QJsonObject &scene, const QString &serverUrl) {
    const QJsonArray files = scene["files"].toArray();
    const QJsonObject file = files.isEmpty() ? QJsonObject() : files.first().toObject();

    // Stash scenes are often untitled and identified by filename only.
    QString title = scene["title"].toString().trimmed();
    if (title.isEmpty()) {
        const QString basename = file["basename"].toString().trimmed();
        if (!basename.isEmpty())
            title = QFileInfo(basename).completeBaseName();
    }
    if (title.isEmpty())
        title = QStringLiteral("SCENE %1").arg(scene["id"].toString());

    QStringList tagNames;
    for (const QJsonValue &v : scene["tags"].toArray()) {
        const QString name = v.toObject()["name"].toString().trimmed();
        if (!name.isEmpty())
            tagNames.append(name);
    }

    // Stash builds paths.stream from the request's base URL (honouring
    // external_host and proxy prefixes) and appends ?apikey=..., so mpv needs
    // no extra header. Current releases return an absolute URL; older ones
    // returned a path-only value, which is resolved against the configured
    // server here.
    const QString streamPath = scene["paths"].toObject()["stream"].toString();
    QString streamUrl;
    if (!streamPath.isEmpty()) {
        const QUrl parsed(streamPath);
        streamUrl = parsed.isRelative()
                        ? QUrl(serverUrl).resolved(parsed).toString()
                        : parsed.toString();
    }

    QVariantMap map;
    map["itemId"] = scene["id"].toString();
    map["title"] = title;
    map["tagsText"] = tagNames.join(QStringLiteral(", "));
    map["streamUrl"] = streamUrl;
    return map;
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------

void StashBackend::load_saved_filters() {
    QJsonObject variables;
    variables["mode"] = QStringLiteral("SCENES");

    postGraphql(kSavedFiltersQuery, variables, QStringLiteral("LOAD SCENE FILTERS FAILED"),
                [this](const QJsonObject &data) {
        emit savedFiltersLoaded(buildFilterRows(data["findSavedFilters"].toArray()));
    });
}

void StashBackend::load_tags() {
    QJsonObject filter;
    filter["per_page"] = -1;
    filter["sort"] = QStringLiteral("name");
    filter["direction"] = QStringLiteral("ASC");

    QJsonObject variables;
    variables["filter"] = filter;

    postGraphql(kTagsQuery, variables, QStringLiteral("LOAD TAGS FAILED"),
                [this](const QJsonObject &data) {
        QVariantList tags;
        for (const QJsonValue &v : data["findTags"].toObject()["tags"].toArray()) {
            const QJsonObject tag = v.toObject();
            const QString name = tag["name"].toString().trimmed();
            if (name.isEmpty())
                continue;
            tags.append(QVariantMap{
                {"id", tag["id"].toString()},
                {"title", name},
            });
        }
        emit tagsLoaded(tags);
    });
}

void StashBackend::requestScenes(const QJsonObject &variables) {
    postGraphql(kScenesQuery, variables, QStringLiteral("LOAD SCENES FAILED"),
                [this](const QJsonObject &data) {
        QVariantList scenes;
        for (const QJsonValue &v : data["findScenes"].toObject()["scenes"].toArray()) {
            const QVariantMap scene = formatScene(v.toObject(), m_serverUrl);
            // A scene with no resolvable stream has no video file to hand off,
            // so showing it would be a dead row.
            if (scene.value("streamUrl").toString().isEmpty())
                continue;
            scenes.append(scene);
        }
        emit scenesLoaded(scenes);
    });
}

void StashBackend::load_scenes(const QVariantMap &findFilter, const QVariantMap &objectFilter) {
    QJsonObject variables;
    variables["filter"] = sceneFindFilter(findFilter);
    if (!objectFilter.isEmpty())
        variables["sceneFilter"] = QJsonObject::fromVariantMap(objectFilter);
    requestScenes(variables);
}

void StashBackend::load_scenes_for_tag(const QString &tagId) {
    if (tagId.isEmpty()) {
        emit errorOccurred(QStringLiteral("LOAD SCENES FAILED: NO TAG SELECTED"));
        return;
    }

    QJsonObject variables;
    variables["filter"] = sceneFindFilter({});
    variables["sceneFilter"] = sceneFilterForTag(tagId);
    requestScenes(variables);
}
