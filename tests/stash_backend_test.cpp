#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "modules/stash/StashBackend.h"

// The helpers under test are private implementation details of the backend, so
// the tests reach them through the friend struct declared in StashBackend.h.
struct StashBackendTestPeer {
    static QString normalizeServerUrl(const QString &url) {
        return StashBackend::normalizeServerUrl(url);
    }
    static QJsonObject sceneFindFilter(const QVariantMap &savedFindFilter) {
        return StashBackend::sceneFindFilter(savedFindFilter);
    }
    static QVariantList buildFilterRows(const QJsonArray &savedFilters) {
        return StashBackend::buildFilterRows(savedFilters);
    }
    static QJsonObject sceneFilterForTag(const QString &tagId) {
        return StashBackend::sceneFilterForTag(tagId);
    }
    static QVariantMap formatScene(const QJsonObject &scene, const QString &serverUrl) {
        return StashBackend::formatScene(scene, serverUrl);
    }
};

class StashBackendTest : public QObject {
    Q_OBJECT

private slots:
    void normalizesServerUrl();
    void stripsGraphqlEndpointFromServerUrl();
    void findFilterKeepsSavedSortAndFetchesEverything();
    void findFilterDropsInvalidDirection();
    void filterRowsPutAllScenesFirst();
    void filterRowsSortByNameAndSkipBlankNames();
    void tagFilterUsesIncludesWithoutDepth();
    void sceneTitleUsesStashTitle();
    void sceneTitleFallsBackToFilename();
    void sceneTitleFallsBackToId();
    void sceneJoinsTags();
    void sceneResolvesRelativeStreamUrl();
    void sceneKeepsAbsoluteStreamUrl();
    void sceneWithoutStreamHasEmptyUrl();
};

void StashBackendTest::normalizesServerUrl() {
    QCOMPARE(StashBackendTestPeer::normalizeServerUrl("  http://host:9999/  "),
             QStringLiteral("http://host:9999"));
    QCOMPARE(StashBackendTestPeer::normalizeServerUrl("https://host/stash"),
             QStringLiteral("https://host/stash"));
}

void StashBackendTest::stripsGraphqlEndpointFromServerUrl() {
    QCOMPARE(StashBackendTestPeer::normalizeServerUrl("http://host:9999/graphql"),
             QStringLiteral("http://host:9999"));
    QCOMPARE(StashBackendTestPeer::normalizeServerUrl("http://host:9999/graphql/"),
             QStringLiteral("http://host:9999"));
    // The check is case-insensitive so a pasted URL still normalizes.
    QCOMPARE(StashBackendTestPeer::normalizeServerUrl("http://host/GraphQL"),
             QStringLiteral("http://host"));
}

void StashBackendTest::findFilterKeepsSavedSortAndFetchesEverything() {
    QVariantMap saved;
    saved["q"] = "  beach  ";
    saved["sort"] = "date";
    saved["direction"] = "DESC";
    saved["page"] = 4;      // a saved filter's paging must not leak into the request
    saved["per_page"] = 25;

    const QJsonObject filter = StashBackendTestPeer::sceneFindFilter(saved);
    QCOMPARE(filter["q"].toString(), QStringLiteral("beach"));
    QCOMPARE(filter["sort"].toString(), QStringLiteral("date"));
    QCOMPARE(filter["direction"].toString(), QStringLiteral("DESC"));
    QCOMPARE(filter["page"].toInt(), 1);
    QCOMPARE(filter["per_page"].toInt(), -1);
}

void StashBackendTest::findFilterDropsInvalidDirection() {
    QVariantMap saved;
    saved["direction"] = "sideways";

    const QJsonObject filter = StashBackendTestPeer::sceneFindFilter(saved);
    QVERIFY(!filter.contains(QStringLiteral("direction")));
    QVERIFY(!filter.contains(QStringLiteral("sort")));
    QVERIFY(!filter.contains(QStringLiteral("q")));
    // "ALL SCENES" passes an empty map, and still asks for the whole list.
    QCOMPARE(StashBackendTestPeer::sceneFindFilter({})["per_page"].toInt(), -1);
}

void StashBackendTest::filterRowsPutAllScenesFirst() {
    QJsonObject alpha;
    alpha["id"] = "1";
    alpha["name"] = "Alpha";
    QJsonObject find;
    find["sort"] = "date";
    find["direction"] = "DESC";
    alpha["find_filter"] = find;
    QJsonObject objectFilter;
    objectFilter["organized"] = true;
    alpha["object_filter"] = objectFilter;

    QJsonArray saved;
    saved.append(alpha);

    const QVariantList rows = StashBackendTestPeer::buildFilterRows(saved);
    QCOMPARE(int(rows.size()), 2);
    QCOMPARE(rows[0].toMap().value("title").toString(), QStringLiteral("ALL SCENES"));
    QVERIFY(rows[0].toMap().value("findFilter").toMap().isEmpty());
    QCOMPARE(rows[1].toMap().value("title").toString(), QStringLiteral("Alpha"));
    QCOMPARE(rows[1].toMap().value("findFilter").toMap().value("sort").toString(),
             QStringLiteral("date"));
    QCOMPARE(rows[1].toMap().value("objectFilter").toMap().value("organized").toBool(), true);
}

void StashBackendTest::filterRowsSortByNameAndSkipBlankNames() {
    QJsonObject zebra;
    zebra["name"] = "zebra";
    QJsonObject alpha;
    alpha["name"] = "Alpha";
    QJsonObject blank;
    blank["name"] = "   ";

    QJsonArray saved;
    saved.append(zebra);
    saved.append(blank);
    saved.append(alpha);

    const QVariantList rows = StashBackendTestPeer::buildFilterRows(saved);
    QCOMPARE(int(rows.size()), 3);
    QCOMPARE(rows[1].toMap().value("title").toString(), QStringLiteral("Alpha"));
    QCOMPARE(rows[2].toMap().value("title").toString(), QStringLiteral("zebra"));
}

void StashBackendTest::tagFilterUsesIncludesWithoutDepth() {
    const QJsonObject filter = StashBackendTestPeer::sceneFilterForTag(QStringLiteral("tag-7"));
    const QJsonObject tags = filter["tags"].toObject();
    QCOMPARE(tags["modifier"].toString(), QStringLiteral("INCLUDES"));
    const QJsonArray values = tags["value"].toArray();
    QCOMPARE(int(values.size()), 1);
    QCOMPARE(values.first().toString(), QStringLiteral("tag-7"));
    QVERIFY(!tags.contains(QStringLiteral("depth")));
}

void StashBackendTest::sceneTitleUsesStashTitle() {
    QJsonObject scene;
    scene["id"] = "7";
    scene["title"] = "  Scene Title  ";

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["title"].toString(), QStringLiteral("Scene Title"));
}

void StashBackendTest::sceneTitleFallsBackToFilename() {
    QJsonObject file;
    file["basename"] = "Some.Scene.Name.mp4";
    QJsonArray files;
    files.append(file);

    QJsonObject scene;
    scene["id"] = "7";
    scene["files"] = files;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["title"].toString(), QStringLiteral("Some.Scene.Name"));
}

void StashBackendTest::sceneTitleFallsBackToId() {
    QJsonObject scene;
    scene["id"] = "42";

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["title"].toString(), QStringLiteral("SCENE 42"));
}

void StashBackendTest::sceneJoinsTags() {
    QJsonObject first;
    first["name"] = "First";
    QJsonObject second;
    second["name"] = "Second";
    QJsonObject blank;
    blank["name"] = "  ";

    QJsonArray tags;
    tags.append(first);
    tags.append(blank);
    tags.append(second);

    QJsonObject scene;
    scene["id"] = "7";
    scene["tags"] = tags;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["tagsText"].toString(), QStringLiteral("First, Second"));
}

void StashBackendTest::sceneResolvesRelativeStreamUrl() {
    QJsonObject paths;
    paths["stream"] = "/scene/7/stream?apikey=abc";
    QJsonObject scene;
    scene["id"] = "7";
    scene["paths"] = paths;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["streamUrl"].toString(),
             QStringLiteral("http://host:9999/scene/7/stream?apikey=abc"));
}

void StashBackendTest::sceneKeepsAbsoluteStreamUrl() {
    QJsonObject paths;
    paths["stream"] = "https://stash.example.com:8443/scene/7/stream?apikey=abc";
    QJsonObject scene;
    scene["id"] = "7";
    scene["paths"] = paths;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://192.168.1.50:9999");
    QCOMPARE(formatted["streamUrl"].toString(),
             QStringLiteral("https://stash.example.com:8443/scene/7/stream?apikey=abc"));
}

void StashBackendTest::sceneWithoutStreamHasEmptyUrl() {
    QJsonObject paths;
    paths["stream"] = "";
    QJsonObject scene;
    scene["id"] = "7";
    scene["paths"] = paths;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QVERIFY(formatted["streamUrl"].toString().isEmpty());
}

QTEST_GUILESS_MAIN(StashBackendTest)

#include "stash_backend_test.moc"
