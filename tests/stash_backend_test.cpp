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
    static QJsonObject sceneFilterInput(const QVariantMap &savedObjectFilter) {
        return StashBackend::sceneFilterInput(savedObjectFilter);
    }
    static QVariantMap formatScene(const QJsonObject &scene, const QString &serverUrl) {
        return StashBackend::formatScene(scene, serverUrl);
    }
    static QString resolutionLabel(int height) {
        return StashBackend::resolutionLabel(height);
    }
    static QVariantList buildMarkerRows(const QJsonArray &rawMarkers, const QString &serverUrl) {
        return StashBackend::buildMarkerRows(rawMarkers, serverUrl);
    }
    static QVariantMap formatMarker(const QJsonObject &marker, const QString &serverUrl) {
        return StashBackend::formatMarker(marker, serverUrl);
    }
    static QString formatTimestamp(double seconds) {
        return StashBackend::formatTimestamp(seconds);
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
    void savedFilterBecomesGraphqlInput();
    void savedFilterBooleanCriterionBecomesBool();
    void savedFilterIsNullKeepsRequiredValue();
    void savedFilterNestedFilterIsConverted();
    void savedFilterResolutionBecomesEnum();
    void savedFilterUnknownResolutionIsDropped();
    void sceneTitleUsesStashTitle();
    void sceneTitleFallsBackToFilename();
    void sceneTitleFallsBackToId();
    void sceneJoinsTags();
    void sceneSummarisesManyTags();
    void sceneIncludesStudioAndResolution();
    void sceneTitleWinsOverFilename();
    void resolutionLabelMatchesStashBuckets();
    void markerCarriesSceneStreamAndOffset();
    void markerFallsBackToPrimaryTag();
    void markerRowsGroupByTagThenSceneThenTime();
    void markerRowsSkipMarkersWithoutAStream();
    void markerTimestampFormatting();
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

void StashBackendTest::savedFilterBecomesGraphqlInput() {
    // Shape taken verbatim from a real Stash 0.32 saved filter
    // (findSavedFilters(mode: SCENES) → object_filter).
    auto labeled = [](const QString &id, const QString &label) {
        return QVariantMap{{"id", id}, {"label", label}};
    };
    auto listValue = [&](const QVariantList &items) {
        return QVariantMap{{"items", items},
                           {"excluded", QVariantList{}},
                           {"depth", 0}};
    };

    QVariantMap studios;
    studios["modifier"] = "INCLUDES";
    studios["value"] = listValue({labeled("16", "Brat Princess")});

    QVariantMap tags;
    tags["modifier"] = "INCLUDES_ALL";
    tags["value"] = listValue({labeled("411", "Cock Milking Machine")});

    QVariantMap created;
    created["modifier"] = "BETWEEN";
    created["value"] = QVariantMap{{"value", "2020-12-31"}, {"value2", "2022-01-01"}};

    QVariantMap rating;
    rating["modifier"] = "NOT_NULL";
    rating["value"] = QVariantMap{{"value", 0}};

    QVariantMap resolution;
    resolution["modifier"] = "EQUALS";
    resolution["value"] = "1080P";

    QVariantMap filter;
    filter["studios"] = studios;
    filter["tags"] = tags;
    filter["created_at"] = created;
    filter["rating100"] = rating;
    filter["resolution"] = resolution;

    const QJsonObject out = StashBackendTestPeer::sceneFilterInput(filter);

    const QJsonObject studioCriterion = out["studios"].toObject();
    QCOMPARE(studioCriterion["modifier"].toString(), QStringLiteral("INCLUDES"));
    QCOMPARE(studioCriterion["value"].toArray().first().toString(), QStringLiteral("16"));
    QCOMPARE(studioCriterion["depth"].toInt(), 0);

    const QJsonObject tagCriterion = out["tags"].toObject();
    QCOMPARE(tagCriterion["modifier"].toString(), QStringLiteral("INCLUDES_ALL"));
    QCOMPARE(tagCriterion["value"].toArray().first().toString(), QStringLiteral("411"));

    const QJsonObject dateCriterion = out["created_at"].toObject();
    QCOMPARE(dateCriterion["modifier"].toString(), QStringLiteral("BETWEEN"));
    QCOMPARE(dateCriterion["value"].toString(), QStringLiteral("2020-12-31"));
    QCOMPARE(dateCriterion["value2"].toString(), QStringLiteral("2022-01-01"));

    const QJsonObject ratingCriterion = out["rating100"].toObject();
    QCOMPARE(ratingCriterion["modifier"].toString(), QStringLiteral("NOT_NULL"));
    QCOMPARE(ratingCriterion["value"].toInt(), 0);

    // The UI label is translated to the GraphQL enum member.
    const QJsonObject resolutionCriterion = out["resolution"].toObject();
    QCOMPARE(resolutionCriterion["modifier"].toString(), QStringLiteral("EQUALS"));
    QCOMPARE(resolutionCriterion["value"].toString(), QStringLiteral("FULL_HD"));
}

void StashBackendTest::savedFilterResolutionBecomesEnum() {
    QVariantMap resolution;
    resolution["modifier"] = "EQUALS";
    resolution["value"] = "4k";        // lowercase, as Stash's UI stores it

    QVariantMap filter;
    filter["resolution"] = resolution;

    const QJsonObject out = StashBackendTestPeer::sceneFilterInput(filter);
    QCOMPARE(out["resolution"].toObject()["value"].toString(), QStringLiteral("FOUR_K"));
}

void StashBackendTest::savedFilterUnknownResolutionIsDropped() {
    // An unrecognised label must not fail the whole filter list.
    QVariantMap resolution;
    resolution["modifier"] = "EQUALS";
    resolution["value"] = "not-a-resolution";

    QVariantMap tags;
    tags["modifier"] = "INCLUDES";
    tags["value"] = QVariantMap{{"items", QVariantList{}},
                                {"excluded", QVariantList{}},
                                {"depth", 0}};

    QVariantMap filter;
    filter["resolution"] = resolution;
    filter["tags"] = tags;

    const QJsonObject out = StashBackendTestPeer::sceneFilterInput(filter);
    QVERIFY(!out.contains(QStringLiteral("resolution")));
    QVERIFY(out.contains(QStringLiteral("tags")));
}

void StashBackendTest::savedFilterBooleanCriterionBecomesBool() {
    // The UI stores booleans as the strings "true"/"false"; the API wants a
    // real JSON boolean for `organized` / `interactive`.
    QVariantMap organized;
    organized["modifier"] = "EQUALS";
    organized["value"] = "true";

    QVariantMap filter;
    filter["organized"] = organized;

    const QJsonObject out = StashBackendTestPeer::sceneFilterInput(filter);
    QCOMPARE(out["organized"].type(), QJsonValue::Bool);
    QCOMPARE(out["organized"].toBool(), true);
}

void StashBackendTest::savedFilterIsNullKeepsRequiredValue() {
    // IntCriterionInput requires `value`, so IS_NULL / NOT_NULL without a saved
    // number still has to send one.
    QVariantMap rating;
    rating["modifier"] = "IS_NULL";

    QVariantMap filter;
    filter["rating100"] = rating;

    const QJsonObject out = StashBackendTestPeer::sceneFilterInput(filter);
    QCOMPARE(out["rating100"].toObject()["value"].toInt(), 0);
    QCOMPARE(out["rating100"].toObject()["value"].type(), QJsonValue::Double);
}

void StashBackendTest::savedFilterNestedFilterIsConverted() {
    auto labeled = [](const QString &id) {
        return QVariantMap{{"id", id}, {"label", id}};
    };
    QVariantMap performers;
    performers["modifier"] = "INCLUDES";
    performers["value"] = QVariantMap{{"items", QVariantList{labeled("7")}},
                                      {"excluded", QVariantList{}},
                                      {"depth", 0}};
    QVariantMap nested;
    nested["performers"] = performers;

    QVariantMap filter;
    filter["performers_filter"] = nested;

    const QJsonObject out = StashBackendTestPeer::sceneFilterInput(filter);
    const QJsonObject nestedOut = out["performers_filter"].toObject();
    QVERIFY(!nestedOut.isEmpty());
    QCOMPARE(nestedOut["performers"].toObject()["value"].toArray().first().toString(),
             QStringLiteral("7"));
    // performers is a MultiCriterionInput, so no depth must leak into it.
    QVERIFY(!nestedOut["performers"].toObject().contains(QStringLiteral("depth")));
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

void StashBackendTest::sceneSummarisesManyTags() {
    QJsonArray tags;
    for (const QString &name : {QStringLiteral("A"), QStringLiteral("B"),
                                QStringLiteral("C"), QStringLiteral("D"),
                                QStringLiteral("E")}) {
        QJsonObject tag;
        tag["name"] = name;
        tags.append(tag);
    }

    QJsonObject scene;
    scene["id"] = "7";
    scene["tags"] = tags;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["tagsText"].toString(), QStringLiteral("A, B, C, +2 MORE"));
}

void StashBackendTest::sceneIncludesStudioAndResolution() {
    QJsonObject studio;
    studio["name"] = "  Brat Princess  ";
    QJsonObject file;
    file["basename"] = "x.mkv";
    file["height"] = 1080;
    QJsonArray files;
    files.append(file);

    QJsonObject scene;
    scene["id"] = "7";
    scene["studio"] = studio;
    scene["files"] = files;

    const QVariantMap formatted = StashBackendTestPeer::formatScene(scene, "http://host:9999");
    QCOMPARE(formatted["studio"].toString(), QStringLiteral("Brat Princess"));
    QCOMPARE(formatted["resolution"].toString(), QStringLiteral("1080P"));
}

void StashBackendTest::sceneTitleWinsOverFilename() {
    QJsonObject file;
    file["basename"] = "Some.Scene.Name.mp4";
    QJsonArray files;
    files.append(file);

    QJsonObject scene;
    scene["id"] = "7";
    scene["title"] = "Real Title";
    scene["files"] = files;

    // The row leads with the Stash title; the filename is only a fallback.
    QCOMPARE(StashBackendTestPeer::formatScene(scene, "http://host:9999")["title"].toString(),
             QStringLiteral("Real Title"));
}

void StashBackendTest::resolutionLabelMatchesStashBuckets() {
    QCOMPARE(StashBackendTestPeer::resolutionLabel(0), QString());
    QCOMPARE(StashBackendTestPeer::resolutionLabel(200), QStringLiteral("144P"));
    QCOMPARE(StashBackendTestPeer::resolutionLabel(480), QStringLiteral("480P"));
    QCOMPARE(StashBackendTestPeer::resolutionLabel(720), QStringLiteral("720P"));
    QCOMPARE(StashBackendTestPeer::resolutionLabel(1079), QStringLiteral("720P"));
    QCOMPARE(StashBackendTestPeer::resolutionLabel(1080), QStringLiteral("1080P"));
    QCOMPARE(StashBackendTestPeer::resolutionLabel(2160), QStringLiteral("4K"));
    QCOMPARE(StashBackendTestPeer::resolutionLabel(6144), QStringLiteral("8K+"));
}

void StashBackendTest::markerCarriesSceneStreamAndOffset() {
    // Shape from a real Stash 0.32 findSceneMarkers response.
    QJsonObject tag;
    tag["name"] = "Pegging";
    QJsonObject scene;
    scene["id"] = "1195";
    scene["title"] = "Worship Her Greatness!";
    QJsonObject paths;
    paths["stream"] = "https://stash.example/scene/1195/stream?apikey=abc";
    scene["paths"] = paths;

    QJsonObject marker;
    marker["id"] = "126";
    marker["title"] = "Anal Insertion";
    marker["seconds"] = 913.0;
    marker["primary_tag"] = tag;
    marker["scene"] = scene;

    const QVariantMap row = StashBackendTestPeer::formatMarker(marker, "https://stash.example");
    QCOMPARE(row["title"].toString(), QStringLiteral("Anal Insertion"));
    QCOMPARE(row["tagText"].toString(), QStringLiteral("Pegging"));
    QCOMPARE(row["sceneTitle"].toString(), QStringLiteral("Worship Her Greatness!"));
    QCOMPARE(row["seconds"].toDouble(), 913.0);
    QCOMPARE(row["timeText"].toString(), QStringLiteral("15:13"));
    // The scene's own stream is played, not the marker clip.
    QCOMPARE(row["streamUrl"].toString(),
             QStringLiteral("https://stash.example/scene/1195/stream?apikey=abc"));
}

void StashBackendTest::markerFallsBackToPrimaryTag() {
    QJsonObject tag;
    tag["name"] = "Facesitting";
    QJsonObject scene;
    scene["id"] = "59";
    QJsonObject paths;
    paths["stream"] = "/scene/59/stream?apikey=abc";
    scene["paths"] = paths;

    QJsonObject marker;
    marker["id"] = "3";
    marker["title"] = "   ";          // untitled marker
    marker["seconds"] = 286;
    marker["primary_tag"] = tag;
    marker["scene"] = scene;

    const QVariantMap row = StashBackendTestPeer::formatMarker(marker, "http://host:9999");
    QCOMPARE(row["title"].toString(), QStringLiteral("Facesitting"));
    // Server-relative stream paths are resolved against serverUrl.
    QCOMPARE(row["streamUrl"].toString(),
             QStringLiteral("http://host:9999/scene/59/stream?apikey=abc"));
}

void StashBackendTest::markerRowsGroupByTagThenSceneThenTime() {
    auto makeMarker = [](const QString &id, const QString &tag, const QString &sceneTitle,
                         double seconds) {
        QJsonObject primaryTag;
        primaryTag["name"] = tag;
        QJsonObject scene;
        scene["id"] = id;
        scene["title"] = sceneTitle;
        QJsonObject paths;
        paths["stream"] = "/scene/" + id + "/stream";
        scene["paths"] = paths;
        QJsonObject marker;
        marker["id"] = id;
        marker["title"] = "M" + id;
        marker["seconds"] = seconds;
        marker["primary_tag"] = primaryTag;
        marker["scene"] = scene;
        return marker;
    };

    QJsonArray raw;
    raw.append(makeMarker("1", "zeta", "Scene B", 100));
    raw.append(makeMarker("2", "alpha", "Scene Z", 30));
    raw.append(makeMarker("3", "alpha", "Scene Z", 10));
    raw.append(makeMarker("4", "alpha", "Scene A", 500));

    const QVariantList rows = StashBackendTestPeer::buildMarkerRows(raw, "http://host:9999");
    QCOMPARE(int(rows.size()), 4);
    const auto row = [&rows](int i) { return rows[i].toMap(); };

    // alpha first (tag), then by scene title, then by time inside a scene.
    QCOMPARE(row(0).value("tagText").toString(), QStringLiteral("alpha"));
    QCOMPARE(row(0).value("sceneTitle").toString(), QStringLiteral("Scene A"));
    QCOMPARE(row(1).value("sceneTitle").toString(), QStringLiteral("Scene Z"));
    QCOMPARE(row(1).value("seconds").toDouble(), 10.0);
    QCOMPARE(row(2).value("sceneTitle").toString(), QStringLiteral("Scene Z"));
    QCOMPARE(row(2).value("seconds").toDouble(), 30.0);
    QCOMPARE(row(3).value("tagText").toString(), QStringLiteral("zeta"));
}

void StashBackendTest::markerRowsSkipMarkersWithoutAStream() {
    QJsonObject broken;
    broken["id"] = "1";
    broken["title"] = "No scene";

    QJsonArray raw;
    raw.append(broken);

    QVERIFY(StashBackendTestPeer::buildMarkerRows(raw, "http://host:9999").isEmpty());
}

void StashBackendTest::markerTimestampFormatting() {
    QCOMPARE(StashBackendTestPeer::formatTimestamp(0), QStringLiteral("0:00"));
    QCOMPARE(StashBackendTestPeer::formatTimestamp(65.9), QStringLiteral("1:05"));
    QCOMPARE(StashBackendTestPeer::formatTimestamp(913), QStringLiteral("15:13"));
    QCOMPARE(StashBackendTestPeer::formatTimestamp(3725), QStringLiteral("1:02:05"));
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
