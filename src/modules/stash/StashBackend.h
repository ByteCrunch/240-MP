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
    // findFilter / objectFilter are a saved filter's find_filter / object_filter
    // as returned by Stash; pass empty maps for "all scenes".
    Q_INVOKABLE void load_scenes(const QVariantMap &findFilter, const QVariantMap &objectFilter);
    Q_INVOKABLE void load_scenes_for_tag(const QString &tagId);

signals:
    void savedFiltersLoaded(const QVariant &filters);
    void tagsLoaded(const QVariant &tags);
    void scenesLoaded(const QVariant &scenes);
    void errorOccurred(const QString &message);

private:
    // Test-only access to the pure helpers below (tests/stash_backend_test.cpp).
    friend struct StashBackendTestPeer;

    static QString normalizeServerUrl(const QString &url);
    // Builds the request's FindFilterType from a saved filter's find_filter:
    // the saved query/sort is kept, but page/per_page are forced so the module
    // always receives the whole list.
    static QJsonObject sceneFindFilter(const QVariantMap &savedFindFilter);
    // "ALL SCENES" first, then the server's saved scene filters by name.
    static QVariantList buildFilterRows(const QJsonArray &savedFilters);
    // SceneFilterType that matches scenes carrying the given tag.
    static QJsonObject sceneFilterForTag(const QString &tagId);
    static QVariantMap formatScene(const QJsonObject &scene, const QString &serverUrl);

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
