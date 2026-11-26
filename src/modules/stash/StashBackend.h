#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

struct StashBackendTestPeer;

// Backend for the Stash module (Stash is a self-hosted media organizer for
// adult scenes; it exposes a GraphQL API at <serverUrl>/graphql).
//
// Connection details live in <dataRoot>/stash.json, deliberately with no
// settings UI so the API key is not written into config.json:
//
//   {
//     "serverUrl": "http://192.168.1.50:9999",
//     "apiKey": "YOUR_STASH_API_KEY"
//   }
//
// The file is only ever read; the app never rewrites it. apiKey is optional
// because a Stash server can be configured to allow unauthenticated GraphQL
// access, but most setups need it (Stash -> Settings -> Security -> API Key).
//
// Browsing is read-only: saved SCENES filters and tags come from the server and
// each list resolves to scenes, which are handed off to mpv (see MpvController).
// Nothing is written back to Stash, so watched state / play counts are
// untouched by this module.
class StashBackend : public QObject {
    Q_OBJECT
public:
    explicit StashBackend(const QString &appRoot, const QString &dataRoot, QObject *parent = nullptr);

    // Config (stash.json)
    Q_INVOKABLE bool has_config() const;
    Q_INVOKABLE QString get_config_path() const;
    Q_INVOKABLE QString get_config_error() const;
    // Re-reads stash.json so an edit applies without restarting the app.
    Q_INVOKABLE void reload_config();

    // Browse
    Q_INVOKABLE void load_saved_filters();
    Q_INVOKABLE void load_tags();
    // Scene markers, grouped by primary tag: a marker plays its scene from the
    // marker's time index.
    Q_INVOKABLE void load_markers();
    // findFilter / objectFilter are a saved filter's find_filter / object_filter
    // as returned by Stash; pass empty maps for "all scenes".
    Q_INVOKABLE void load_scenes(const QVariantMap &findFilter, const QVariantMap &objectFilter);
    Q_INVOKABLE void load_scenes_for_tag(const QString &tagId);

    // Preflight for the Player: a reverse proxy or SSO in front of Stash
    // can redirect /scene/…/stream to a login page which is unreadable for mpv.
    Q_INVOKABLE void check_stream(const QString &url);

signals:
    void savedFiltersLoaded(const QVariant &filters);
    void tagsLoaded(const QVariant &tags);
    void markersLoaded(const QVariant &markers);
    void scenesLoaded(const QVariant &scenes);
    void streamCheckFinished(bool ok, const QString &error);
    void errorOccurred(const QString &message);

private:
    // Test-only access to the pure helpers below (tests/stash_backend_test.cpp).
    friend struct StashBackendTestPeer;

    static QString normalizeServerUrl(const QString &url);
    // Builds the request's FindFilterType from a saved filter's find_filter:
    // the saved query/sort is kept, but page/per_page are forced so the module
    // always receives the whole list.
    static QJsonObject sceneFindFilter(const QVariantMap &savedFindFilter);
    // Stash stores a saved filter's object_filter in its *UI* shape
    // ({modifier, value: {items: [{id,label}], depth, excluded}} and
    // {modifier, value: {value, value2}}), which is not a valid SceneFilterType
    // input. This converts it to the GraphQL shape
    // ({value: [id], depth, excludes} / {value, value2}).
    static QJsonObject sceneFilterInput(const QVariantMap &savedObjectFilter);
    static QJsonValue criterionInput(const QString &key, const QVariant &savedCriterion);
    // Human label for a file height, using Stash's own bucket boundaries
    // (pkg/models/resolution.go: 1080-1439 = FULL_HD, 1920-2559 = FOUR_K, …).
    static QString resolutionLabel(int height);
    // "ALL SCENES" first, then the server's saved scene filters by name.
    static QVariantList buildFilterRows(const QJsonArray &savedFilters);
    // SceneFilterType that matches scenes carrying the given tag.
    static QJsonObject sceneFilterForTag(const QString &tagId);
    static QVariantMap formatScene(const QJsonObject &scene, const QString &serverUrl);
    // Formats, filters and orders scene markers for the MARKERS list: grouped
    // by primary tag, then scene title, then time index.
    static QVariantList buildMarkerRows(const QJsonArray &rawMarkers, const QString &serverUrl);
    static QVariantMap formatMarker(const QJsonObject &marker, const QString &serverUrl);
    // "4:46" / "1:02:03" — the marker's time index, shown in the row.
    static QString formatTimestamp(double seconds);
    // Stash returns stream paths either absolute or server-relative.
    static QString resolveStreamUrl(const QString &streamPath, const QString &serverUrl);

    QString configPath() const;
    void loadConfig();
    void postGraphql(const QString &query, const QJsonObject &variables,
                     const QString &errorPrefix,
                     std::function<void(const QJsonObject &data)> onData);
    void requestScenes(const QJsonObject &variables);
    void ignoreSslErrors(QNetworkReply *reply) const;

    QString m_appRoot;
    QString m_dataRoot;
    QNetworkAccessManager *m_nam;

    QString m_serverUrl;
    QString m_apiKey;
    QString m_configError;
};
