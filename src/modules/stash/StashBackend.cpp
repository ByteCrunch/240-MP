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
        const QByteArray body = reply->readAll();

        // Stash answers a bad filter/query with 422 and a GraphQL error body, so
        // prefer the server's own message over Qt's generic transport text.
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        const bool jsonBody = err.error == QJsonParseError::NoError && doc.isObject();
        const QJsonArray errors = jsonBody ? doc.object()["errors"].toArray() : QJsonArray();

        if (reply->error() != QNetworkReply::NoError) {
            if (!errors.isEmpty()) {
                QString message = errors.first().toObject()["message"].toString().trimmed();
                if (message.isEmpty())
                    message = reply->errorString();
                emit errorOccurred(errorPrefix + ": " + message);
            } else if (status == 401 || status == 403) {
                emit errorOccurred(QStringLiteral("AUTH FAILED - CHECK APIKEY IN STASH.JSON"));
            } else {
                emit errorOccurred(errorPrefix + ": " + reply->errorString());
            }
            return;
        }

        if (!jsonBody) {
            emit errorOccurred(QStringLiteral("INVALID RESPONSE FROM SERVER - CHECK SERVERURL"));
            return;
        }

        const QJsonObject root = doc.object();
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

namespace {

// GraphQL SceneFilterType fields whose CriterionInput is a bare Boolean.
const QSet<QString> kBooleanFields = {
    QStringLiteral("organized"),
    QStringLiteral("interactive"),
};

// ...whose input is a plain String rather than {value, modifier}.
const QSet<QString> kPlainStringFields = {
    QStringLiteral("has_markers"),
    QStringLiteral("is_missing"),
};

// ...whose CriterionInput takes a non-optional numeric `value`, so IS_NULL /
// NOT_NULL still needs a number (the Stash UI sends 0).
const QSet<QString> kIntFields = {
    QStringLiteral("rating100"),      QStringLiteral("o_counter"),
    QStringLiteral("file_count"),     QStringLiteral("framerate"),
    QStringLiteral("bitrate"),        QStringLiteral("duration"),
    QStringLiteral("tag_count"),      QStringLiteral("performer_count"),
    QStringLiteral("performer_age"),  QStringLiteral("interactive_speed"),
    QStringLiteral("resume_time"),    QStringLiteral("play_count"),
    QStringLiteral("play_duration"),  QStringLiteral("stash_id_count"),
};

// ...whose CriterionInput is HierarchicalMultiCriterionInput (takes `depth`);
// every other list criterion is a MultiCriterionInput, which would reject it.
const QSet<QString> kHierarchicalFields = {
    QStringLiteral("studios"),
    QStringLiteral("tags"),
    QStringLiteral("groups"),
    QStringLiteral("performer_tags"),
};

// Saved filters store the resolution as the UI's label ("4k", "1080p", "Huge"),
// not as the GraphQL ResolutionEnum member.
QString resolutionEnumValue(const QString &label) {
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("144p"),  QStringLiteral("VERY_LOW")},
        {QStringLiteral("240p"),  QStringLiteral("LOW")},
        {QStringLiteral("360p"),  QStringLiteral("R360P")},
        {QStringLiteral("480p"),  QStringLiteral("STANDARD")},
        {QStringLiteral("540p"),  QStringLiteral("WEB_HD")},
        {QStringLiteral("720p"),  QStringLiteral("STANDARD_HD")},
        {QStringLiteral("1080p"), QStringLiteral("FULL_HD")},
        {QStringLiteral("1440p"), QStringLiteral("QUAD_HD")},
        {QStringLiteral("4k"),    QStringLiteral("FOUR_K")},
        {QStringLiteral("5k"),    QStringLiteral("FIVE_K")},
        {QStringLiteral("6k"),    QStringLiteral("SIX_K")},
        {QStringLiteral("7k"),    QStringLiteral("SEVEN_K")},
        {QStringLiteral("8k"),    QStringLiteral("EIGHT_K")},
        {QStringLiteral("huge"),  QStringLiteral("HUGE")},
    };
    return kMap.value(label.trimmed().toLower());
}

// Deep-converts a QML/QVariant value into JSON (keeps nested maps/lists intact).
QJsonValue toJsonValue(const QVariant &v) {
    const int type = v.metaType().id();
    if (type == QMetaType::QVariantMap || type == QMetaType::QVariantHash) {
        const QVariantMap map = v.toMap();
        QJsonObject obj;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            obj.insert(it.key(), toJsonValue(it.value()));
        return obj;
    }
    if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
        QJsonArray arr;
        const QVariantList list = v.toList();
        for (const QVariant &item : list)
            arr.append(toJsonValue(item));
        return arr;
    }
    if (!v.isValid())
        return QJsonValue::Null;
    return QJsonValue::fromVariant(v);
}

} // namespace

// ---------------------------------------------------------------------------
// Saved-filter object_filter → SceneFilterType input
// ---------------------------------------------------------------------------
//
// Stash's UI stores a saved filter's criteria in its own shape, which the API
// rejects if replayed verbatim:
//
//   "tags":     {"modifier":"INCLUDES_ALL",
//                "value":{"depth":0,"excluded":[],"items":[{"id":"411","label":"…"}]}}
//   "created_at":{"modifier":"BETWEEN","value":{"value":"2020-12-31","value2":"2022-01-01"}}
//   "organized": {"modifier":"EQUALS","value":"true"}
//
// The GraphQL inputs want:
//
//   "tags":     {"modifier":"INCLUDES_ALL","value":["411"],"depth":0,"excludes":[]}
//   "created_at":{"modifier":"BETWEEN","value":"2020-12-31","value2":"2022-01-01"}
//   "organized": true
//
// (mirrors ui/v2.5/src/models/list-filter/criteria/criterion.ts toCriterionInput)

QJsonValue StashBackend::criterionInput(const QString &key, const QVariant &savedCriterion) {
    if (savedCriterion.metaType().id() != QMetaType::QVariantMap)
        return toJsonValue(savedCriterion);

    const QVariantMap criterion = savedCriterion.toMap();
    if (!criterion.contains(QStringLiteral("modifier")))
        return toJsonValue(savedCriterion);

    const QString modifier = criterion.value(QStringLiteral("modifier")).toString();
    const QVariant value = criterion.value(QStringLiteral("value"));

    if (kBooleanFields.contains(key)) {
        const QString text = value.toString().trimmed().toLower();
        return text == QLatin1String("true");
    }
    if (kPlainStringFields.contains(key))
        return QJsonValue(value.toString());

    // Resolution is the one criterion whose value has to be translated to an
    // enum member; an unknown label drops the criterion (as the Stash UI does)
    // rather than failing the whole list.
    if (key == QLatin1String("resolution")) {
        const QString enumValue = resolutionEnumValue(value.toString());
        if (enumValue.isEmpty())
            return QJsonValue(QJsonValue::Undefined);
        QJsonObject input;
        input["modifier"] = modifier;
        input["value"] = enumValue;
        return input;
    }

    QJsonObject input;
    input["modifier"] = modifier;

    if (value.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap val = value.toMap();

        if (val.contains(QStringLiteral("items"))) {
            // List criterion: [{id,label}] → [id]; the UI keeps excluded ids and
            // the include-descendants depth alongside them.
            QJsonArray ids;
            for (const QVariant &item : val.value(QStringLiteral("items")).toList())
                ids.append(item.toMap().value(QStringLiteral("id")).toString());
            input["value"] = ids;

            QJsonArray excludes;
            for (const QVariant &item : val.value(QStringLiteral("excluded")).toList())
                excludes.append(item.toMap().value(QStringLiteral("id")).toString());
            if (!excludes.isEmpty())
                input["excludes"] = excludes;

            if (kHierarchicalFields.contains(key)) {
                // EQUALS means "this exact tag/studio", i.e. no descendants.
                input["depth"] = modifier == QLatin1String("EQUALS")
                                     ? 0
                                     : val.value(QStringLiteral("depth")).toInt();
            }
        } else {
            // Range criterion: {value, value2} → flat value / value2.
            const QVariant inner = val.value(QStringLiteral("value"));
            const QVariant inner2 = val.value(QStringLiteral("value2"));
            if (inner.isValid() && !inner.isNull())
                input["value"] = toJsonValue(inner);
            else if (kIntFields.contains(key))
                input["value"] = 0;
            if (inner2.isValid() && !inner2.isNull())
                input["value2"] = toJsonValue(inner2);
        }
        return input;
    }

    if (!value.isValid() || value.isNull()) {
        // IntCriterionInput requires a value even for IS_NULL / NOT_NULL.
        if (kIntFields.contains(key))
            input["value"] = 0;
        return input;
    }

    input["value"] = toJsonValue(value);
    return input;
}

QJsonObject StashBackend::sceneFilterInput(const QVariantMap &savedObjectFilter) {
    QJsonObject out;
    for (auto it = savedObjectFilter.constBegin(); it != savedObjectFilter.constEnd(); ++it) {
        // Nested filters (performers_filter, files_filter, …) carry the same
        // criterion shapes one level down.
        if (it.key().endsWith(QLatin1String("_filter"))
            && it.value().metaType().id() == QMetaType::QVariantMap) {
            out.insert(it.key(), sceneFilterInput(it.value().toMap()));
            continue;
        }
        const QJsonValue criterion = criterionInput(it.key(), it.value());
        // Undefined means "this criterion cannot be expressed" (e.g. an
        // unrecognised resolution label) — omit it instead of sending bad input.
        if (!criterion.isUndefined())
            out.insert(it.key(), criterion);
    }
    return out;
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
    // Saved filters arrive in the UI shape — convert before sending.
    if (!objectFilter.isEmpty())
        variables["sceneFilter"] = sceneFilterInput(objectFilter);
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

void StashBackend::check_stream(const QString &url) {
    if (url.isEmpty()) {
        emit streamCheckFinished(false, QStringLiteral("NO PLAYABLE STREAM"));
        return;
    }

    QNetworkRequest req{QUrl(url)};
    // Do not follow a redirect: it is exactly the case we want to report.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Range", "bytes=0-1023");

    auto *reply = m_nam->get(req);
    ignoreSslErrors(reply);

    // Capture the original URL: Qt follows redirects regardless of the request
    // policy and updates both url() and request().url() to the final target, so
    // only a copy taken here can tell us the stream was bounced.
    const QUrl requested{QUrl(url)};

    connect(reply, &QNetworkReply::finished, this, [this, reply, requested]() {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString contentType =
            reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        const QString proxyHint =
            QStringLiteral("\n\nA REVERSE PROXY OR LOGIN PAGE IS IN FRONT OF STASH\n"
                           "SEE MODULES/STASH/README.MD");

        const QUrl landed(reply->url());
        if (!landed.isEmpty() && landed.host() != requested.host()) {
            emit streamCheckFinished(
                false,
                QStringLiteral("STREAM REDIRECTED TO %1").arg(landed.host()) + proxyHint);
            return;
        }
        if (status >= 300 && status < 400) {
            const QString target =
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().host();
            emit streamCheckFinished(
                false,
                QStringLiteral("STREAM WAS REDIRECTED%1")
                        .arg(target.isEmpty() ? QString() : QStringLiteral(" TO ") + target)
                    + proxyHint);
            return;
        }
        if (status == 401 || status == 403) {
            emit streamCheckFinished(
                false,
                QStringLiteral("STREAM REFUSED (HTTP %1)").arg(status) + proxyHint);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit streamCheckFinished(false, reply->errorString());
            return;
        }
        // A proxy that answers 200 with an HTML page (rather than redirecting)
        // would otherwise reach mpv as an unreadable file.
        if (contentType.startsWith(QLatin1String("text/html"))) {
            emit streamCheckFinished(
                false, QStringLiteral("THE SERVER RETURNED A LOGIN PAGE") + proxyHint);
            return;
        }

        emit streamCheckFinished(true, QString());
    });
}
