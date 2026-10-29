// tests/services/search_service_tests.cpp

#include "services/search_service.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace {

using namespace xdstarter;

QStringList resultIds(const SearchResult& result) {
    QStringList ids;
    for (const auto& match : result.matches)
        ids.append(match.targetId);
    return ids;
}

QStringList serviceIds(const SearchService& service) {
    QStringList ids;
    for (const auto& match : service.results())
        ids.append(match.targetId);
    return ids;
}

QVector<SearchEntry> referenceEntries(int count = 4096) {
    QVector<SearchEntry> entries;
    entries.reserve(count);
    for (int index = 0; index < count; ++index) {
        const auto id = QStringLiteral("fixture-%1").arg(index, 4, 10, QLatin1Char('0'));
        const QStringList names{QStringLiteral("Microsoft Paint %1"),
                                QStringLiteral("画图工具 %1"), QStringLiteral("重庆银行 %1"),
                                QStringLiteral("绿色应用 %1"), QStringLiteral("Visual Studio %1"),
                                QStringLiteral("Windows Terminal %1"),
                                QStringLiteral("计算器 Calculator %1"),
                                QStringLiteral("离线文档 %1")};
        entries.append({id, names.at(index % names.size()).arg(index),
                        {QStringLiteral("常用工具 %1").arg(index)}});
    }
    return entries;
}

QStringList referenceQueries() {
    QStringList queries;
    for (int variation = 0; variation < 10; ++variation) {
        const int base = variation * 8;
        queries.append(QStringLiteral("paint %1").arg(base));
        queries.append(QStringLiteral("huatugongju %1").arg(base + 1));
        queries.append(QStringLiteral("htgj %1").arg(base + 1));
        queries.append(QStringLiteral("chongqingyinhang %1").arg(base + 2));
        queries.append(QStringLiteral("cqyh %1").arg(base + 2));
        queries.append(QStringLiteral("lvseyingyong %1").arg(base + 3));
        queries.append(QStringLiteral("calculator %1").arg(base + 6));
        queries.append(QStringLiteral("找不到应用 %1").arg(base));
        queries.append(QStringLiteral("Studio %1").arg(base + 4));
        queries.append(QStringLiteral("常用工具 %1").arg(base + 7));
    }
    return queries;
}

QJsonObject latencyStatistics(QVector<double> elapsed, int checksum) {
    std::sort(elapsed.begin(), elapsed.end());
    return {{QStringLiteral("p50Milliseconds"), elapsed.at(49)},
            {QStringLiteral("p95Milliseconds"), elapsed.at(94)},
            {QStringLiteral("maximumMilliseconds"), elapsed.last()},
            {QStringLiteral("matchCountChecksum"), checksum}};
}

bool measureServiceQuery(SearchService& service, const QString& query, double& milliseconds) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool complete = false;
    bool timedOut = false;
    QObject::connect(&service, &SearchService::stateChanged, &loop, [&] {
        if (!service.busy() && service.query() == query) {
            complete = true;
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        timedOut = true;
        loop.quit();
    });
    QElapsedTimer timer;
    timer.start();
    timeout.start(5000);
    service.setQuery(query);
    if (!complete)
        loop.exec();
    milliseconds = double(timer.nsecsElapsed()) / 1000000.0;
    return complete && !timedOut;
}

int runReferenceBenchmark(int entryCount) {
    const auto entries = referenceEntries(entryCount);
    const auto queries = referenceQueries();
    SearchIndex index;
    QElapsedTimer timer;
    timer.start();
    index.setEntries(entries);
    const double buildMilliseconds = double(timer.nsecsElapsed()) / 1000000.0;
    for (int sample = 0; sample < 20; ++sample)
        (void)index.search(queries.at(sample));
    QVector<double> elapsed;
    QVector<QStringList> expectedIds;
    QVector<int> expectedCounts;
    int checksum = 0;
    for (const auto& query : queries) {
        timer.restart();
        const auto result = index.search(query);
        elapsed.append(double(timer.nsecsElapsed()) / 1000000.0);
        expectedIds.append(resultIds(result));
        expectedCounts.append(result.matchCount);
        checksum += result.matchCount;
    }
    const auto directLookup = latencyStatistics(elapsed, checksum);

    SearchService service;
    service.setEntries(entries);
    double serviceIndexReadyMilliseconds = 0;
    if (!measureServiceQuery(service, QStringLiteral("__index-ready-probe__"),
                             serviceIndexReadyMilliseconds)) {
        std::fprintf(stderr, "SearchService benchmark timed out while preparing the index.\n");
        return 2;
    }
    for (int sample = 0; sample < 20; ++sample) {
        double ignored = 0;
        if (!measureServiceQuery(service, queries.at(sample), ignored)) {
            std::fprintf(stderr, "SearchService benchmark timed out during warmup.\n");
            return 2;
        }
    }
    QVector<double> roundTripElapsed;
    QJsonArray samples;
    int roundTripChecksum = 0;
    for (int sample = 0; sample < queries.size(); ++sample) {
        const auto& query = queries.at(sample);
        double milliseconds = 0;
        if (!measureServiceQuery(service, query, milliseconds)) {
            std::fprintf(stderr, "SearchService benchmark timed out during measurement.\n");
            return 2;
        }
        roundTripElapsed.append(milliseconds);
        roundTripChecksum += service.matchCount();
        if (service.matchCount() != expectedCounts.at(sample) ||
            serviceIds(service) != expectedIds.at(sample)) {
            std::fprintf(stderr, "SearchService benchmark disagrees with direct lookup at sample %d.\n",
                         sample);
            return 3;
        }
        samples.append(QJsonObject{{QStringLiteral("query"), query},
                                   {QStringLiteral("directMilliseconds"), elapsed.at(sample)},
                                   {QStringLiteral("serviceMilliseconds"), milliseconds},
                                   {QStringLiteral("matchCount"), expectedCounts.at(sample)}});
    }
    const auto serviceRoundTrip = latencyStatistics(roundTripElapsed, roundTripChecksum);
    if (roundTripChecksum != checksum) {
        std::fprintf(stderr, "SearchService benchmark disagrees with direct lookup results.\n");
        return 3;
    }
    QFile executable(QCoreApplication::applicationFilePath());
    QCryptographicHash binaryHash(QCryptographicHash::Sha256);
    const bool hashed = executable.open(QIODevice::ReadOnly) && binaryHash.addData(&executable);
#ifdef _MSC_FULL_VER
    const QString compiler = QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#elif defined(__clang__)
    const QString compiler = QStringLiteral("Clang ") + QString::fromLatin1(__clang_version__);
#elif defined(__GNUC__)
    const QString compiler = QStringLiteral("GCC ") + QString::fromLatin1(__VERSION__);
#else
    const QString compiler = QStringLiteral("unknown");
#endif
#ifdef NDEBUG
    constexpr bool releaseNdebug = true;
#else
    constexpr bool releaseNdebug = false;
#endif
    const QJsonObject report{
        {QStringLiteral("reportVersion"), 2},
        {QStringLiteral("dataset"), QStringLiteral("synthetic-offline-applications-v1")},
        {QStringLiteral("querySet"), QStringLiteral("ten-families-ten-numbers-v2")},
        {QStringLiteral("entries"), entryCount},
        {QStringLiteral("binarySha256"), hashed ? QString::fromLatin1(binaryHash.result().toHex())
                                                : QString()},
        {QStringLiteral("qtBuildVersion"), QStringLiteral(QT_VERSION_STR)},
        {QStringLiteral("qtRuntimeVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("compiler"), compiler},
        {QStringLiteral("buildAbi"), QSysInfo::buildAbi()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("monotonicClock"), QElapsedTimer::isMonotonic()},
        {QStringLiteral("releaseNdebug"), releaseNdebug},
        {QStringLiteral("aliasesPerEntry"), 1},
        {QStringLiteral("warmupQueries"), 20},
        {QStringLiteral("measuredQueries"), int(elapsed.size())},
        {QStringLiteral("distinctMeasuredQueries"), int(queries.size())},
        {QStringLiteral("resultLimit"), SearchIndex::maximumResults},
        {QStringLiteral("buildMilliseconds"), buildMilliseconds},
        {QStringLiteral("searchP50Milliseconds"),
         directLookup.value(QStringLiteral("p50Milliseconds"))},
        {QStringLiteral("searchP95Milliseconds"),
         directLookup.value(QStringLiteral("p95Milliseconds"))},
        {QStringLiteral("searchMaximumMilliseconds"),
         directLookup.value(QStringLiteral("maximumMilliseconds"))},
        {QStringLiteral("matchCountChecksum"), checksum},
        {QStringLiteral("directLookup"), directLookup},
        {QStringLiteral("serviceIndexReadyMilliseconds"), serviceIndexReadyMilliseconds},
        {QStringLiteral("serviceRoundTrip"), serviceRoundTrip},
        {QStringLiteral("serviceRoundTripBoundary"),
         QStringLiteral("setQuery-to-GUI-results-accepted; includes-worker-queue; excludes-QML")},
        {QStringLiteral("perQueryTimeoutMilliseconds"), 5000},
        {QStringLiteral("perQueryResultsIdentical"), true},
        {QStringLiteral("samples"), samples}};
    const auto output = QJsonDocument(report).toJson(QJsonDocument::Compact);
    std::fprintf(stdout, "%s\n", output.constData());
    return 0;
}

class SearchServiceTests final : public QObject {
    Q_OBJECT

  private slots:
    void literalMatchingHasStablePriorityAndCanonicalNames();
    void duplicateTargetsMergeAliasesWithoutDuplicateResults();
    void allTileOverridesShareOneBoundedGlobalBudget();
    void pinyinMatchesFullInitialsPolyphonicAndMixedNames();
    void commonPolyphonicAlternativesRemainSearchable();
    void normalizationKeepsLiteralPunctuationAndAcceptsUmlautVariants();
    void emptyUnknownAndLongQueriesStayBounded();
    void resultLimitPreservesTotalMatchCount();
    void latestQueryWinsWithoutPublishingOldMatches();
    void refreshedIndexCannotPublishRemovedTargets();
    void clearingQueryInvalidatesWorkAndDestructionJoins();
};

void SearchServiceTests::allTileOverridesShareOneBoundedGlobalBudget() {
    SearchEntry entry{QStringLiteral("shared"), QStringLiteral("Original application"), {}};
    for (int index = 0; index < SearchIndex::maximumTileDisplayNames + 1; ++index)
        entry.tileDisplayNames.append(QStringLiteral("Custom%1end").arg(index));
    SearchIndex index;
    index.setEntries({entry});
    for (const auto& query : {QStringLiteral("Custom0end"), QStringLiteral("Custom511end")}) {
        QCOMPARE(index.search(query).matchCount, 1);
        QCOMPARE(index.search(query).matches.first().targetId, QStringLiteral("shared"));
        QCOMPARE(index.search(query).matches.first().displayName, entry.displayName);
    }
    QVERIFY(index.search(QStringLiteral("Custom512end")).matches.isEmpty());
    SearchEntry another{QStringLiteral("another"), QStringLiteral("Another application"), {}};
    another.tileDisplayNames.append(QStringLiteral("OverGlobalBudget"));
    index.setEntries({entry, another});
    QVERIFY(index.search(QStringLiteral("OverGlobalBudget")).matches.isEmpty());
    entry.tileDisplayNames.clear();
    index.setEntries({entry});
    QVERIFY(index.search(QStringLiteral("Custom0end")).matches.isEmpty());
}

void SearchServiceTests::literalMatchingHasStablePriorityAndCanonicalNames() {
    SearchIndex index;
    index.setEntries({{QStringLiteral("inside"), QStringLiteral("My Tool"), {}},
                      {QStringLiteral("prefix-b"), QStringLiteral("Toolbox"), {}},
                      {QStringLiteral("exact"), QStringLiteral("TOOL"), {}},
                      {QStringLiteral("prefix-a"), QStringLiteral("toolbox"), {}},
                      {QStringLiteral("irrelevant"), QStringLiteral("Other"), {}}});
    const auto result = index.search(QStringLiteral("tool"));
    QCOMPARE(result.matchCount, 4);
    QCOMPARE(resultIds(result),
             QStringList({QStringLiteral("exact"), QStringLiteral("prefix-a"),
                          QStringLiteral("prefix-b"), QStringLiteral("inside")}));
    QCOMPARE(result.matches.at(0).rank, 0);
    QCOMPARE(result.matches.at(1).rank, 1);
    QCOMPARE(result.matches.at(3).rank, 2);
    QCOMPARE(result.matches.at(0).displayName, QStringLiteral("TOOL"));
    QCOMPARE(resultIds(index.search(QStringLiteral("TOOL"))), resultIds(result));
    QCOMPARE(resultIds(index.search(QStringLiteral(" Ｔｏｏｌ "))), resultIds(result));
}

void SearchServiceTests::duplicateTargetsMergeAliasesWithoutDuplicateResults() {
    SearchIndex index;
    index.setEntries({{QStringLiteral("paint"), QStringLiteral("Paint"), {}},
                      {QStringLiteral("paint"), QStringLiteral("画图"),
                       {QStringLiteral("绘图工具"), QStringLiteral("PAINT")}},
                      {QStringLiteral("persisted"), QStringLiteral("离线应用"),
                       {QStringLiteral("Offline Tool")}},
                      {{}, QStringLiteral("No target"), {}},
                      {QStringLiteral("unnamed"), {}, {}}});
    for (const auto& query : {QStringLiteral("paint"), QStringLiteral("画图"),
                              QStringLiteral("huatu"), QStringLiteral("绘图工具")}) {
        const auto result = index.search(query);
        QCOMPARE(result.matchCount, 1);
        QCOMPARE(result.matches.first().targetId, QStringLiteral("paint"));
        QCOMPARE(result.matches.first().displayName, QStringLiteral("Paint"));
    }
    QCOMPARE(resultIds(index.search(QStringLiteral("offline"))),
             QStringList{QStringLiteral("persisted")});
}

void SearchServiceTests::pinyinMatchesFullInitialsPolyphonicAndMixedNames() {
    SearchIndex index;
    index.setEntries({{QStringLiteral("paint"), QStringLiteral("画图"), {}},
                      {QStringLiteral("bank"), QStringLiteral("重庆银行"), {}},
                      {QStringLiteral("terminal"), QStringLiteral("Windows 终端"), {}},
                      {QStringLiteral("literal"), QStringLiteral("huatu editor"), {}}});
    const auto paint = index.search(QStringLiteral("huatu"));
    QCOMPARE(resultIds(paint),
             QStringList({QStringLiteral("literal"), QStringLiteral("paint")}));
    QCOMPARE(paint.matches.at(0).rank, 1);
    QCOMPARE(paint.matches.at(1).rank, 3);
    const auto initials = index.search(QStringLiteral("ht"));
    QCOMPARE(resultIds(initials), QStringList{QStringLiteral("paint")});
    QCOMPARE(initials.matches.first().rank, 4);
    for (const auto& query : {QStringLiteral("chongqingyinhang"), QStringLiteral("cqyh"),
                              QStringLiteral("zhongqingyinxing"), QStringLiteral("qingyin"),
                              QStringLiteral("qyh")}) {
        QCOMPARE(resultIds(index.search(query)), QStringList{QStringLiteral("bank")});
    }
    for (const auto& query : {QStringLiteral("windowszhongduan"),
                              QStringLiteral("windows zd"), QStringLiteral("zhongduan")}) {
        QCOMPARE(resultIds(index.search(query)), QStringList{QStringLiteral("terminal")});
    }
}

void SearchServiceTests::normalizationKeepsLiteralPunctuationAndAcceptsUmlautVariants() {
    SearchIndex index;
    index.setEntries({{QStringLiteral("green"), QStringLiteral("绿色应用"), {}},
                      {QStringLiteral("cpp"), QStringLiteral("C++ Reference"), {}},
                      {QStringLiteral("unknown"), QStringLiteral("画😀图"), {}}});
    for (const auto& query : {QStringLiteral("lvse"), QStringLiteral("lüse"),
                              QStringLiteral("lǜsè"), QStringLiteral("lu:se"),
                              QStringLiteral("LＶSE")}) {
        QCOMPARE(resultIds(index.search(query)), QStringList{QStringLiteral("green")});
    }
    QCOMPARE(resultIds(index.search(QStringLiteral("c++"))),
             QStringList{QStringLiteral("cpp")});
    QCOMPARE(resultIds(index.search(QStringLiteral("huatu"))), QStringList{});
    QCOMPARE(resultIds(index.search(QStringLiteral("++"))),
             QStringList{QStringLiteral("cpp")});
    QCOMPARE(index.search(QStringLiteral("!!!")).matchCount, 0);
    QCOMPARE(resultIds(index.search(QStringLiteral("u:"))),
             QStringList{QStringLiteral("green")});
}

void SearchServiceTests::commonPolyphonicAlternativesRemainSearchable() {
    const QVector<QPair<QString, QStringList>> cases{
        {QStringLiteral("乐"), {QStringLiteral("le"), QStringLiteral("yue")}},
        {QStringLiteral("长"), {QStringLiteral("chang"), QStringLiteral("zhang")}},
        {QStringLiteral("重"), {QStringLiteral("chong"), QStringLiteral("zhong")}},
        {QStringLiteral("行"), {QStringLiteral("hang"), QStringLiteral("xing")}}};
    for (const auto& [name, queries] : cases) {
        SearchIndex index;
        index.setEntries({{QStringLiteral("polyphonic"), name, {}}});
        for (const auto& query : queries) {
            const auto result = index.search(query);
            QCOMPARE(resultIds(result), QStringList{QStringLiteral("polyphonic")});
            QCOMPARE(result.matches.first().rank, 3);
        }
    }
}

void SearchServiceTests::emptyUnknownAndLongQueriesStayBounded() {
    SearchIndex index;
    index.setEntries({{QStringLiteral("long"), QString(300, QLatin1Char('a')), {}},
                      {QStringLiteral("ligature"), QString(256, QChar(0xfb03)), {}}});
    QCOMPARE(index.search({}).matchCount, 0);
    QCOMPARE(index.search(QStringLiteral(" \t\n　")).matchCount, 0);
    QCOMPARE(index.search(QStringLiteral("not installed")).matchCount, 0);
    const auto result = index.search(QString(200, QLatin1Char('a')));
    QCOMPARE(result.matchCount, 1);
    QCOMPARE(result.matches.first().displayName.size(), SearchIndex::maximumNameLength);
    // NFKC can expand a single Unicode character to several ASCII characters.
    QCOMPARE(index.search(QString(128, QChar(0xfb03))).matchCount, 1);
}

void SearchServiceTests::resultLimitPreservesTotalMatchCount() {
    QVector<SearchEntry> entries;
    for (int number = 149; number >= 0; --number) {
        const auto suffix = QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'));
        entries.append({suffix, QStringLiteral("Application ") + suffix, {}});
    }
    SearchIndex index;
    index.setEntries(entries);
    const auto result = index.search(QStringLiteral("app"));
    QCOMPARE(result.matchCount, 150);
    QCOMPARE(result.matches.size(), SearchIndex::maximumResults);
    QCOMPARE(result.matches.first().targetId, QStringLiteral("000"));
    QCOMPARE(result.matches.last().targetId, QStringLiteral("099"));
    std::reverse(entries.begin(), entries.end());
    index.setEntries(entries);
    QCOMPARE(resultIds(index.search(QStringLiteral("app"))), resultIds(result));
}

void SearchServiceTests::latestQueryWinsWithoutPublishingOldMatches() {
    SearchService service;
    service.setEntries({{QStringLiteral("paint"), QStringLiteral("画图 Paint"), {}},
                        {QStringLiteral("terminal"), QStringLiteral("Windows Terminal"), {}}});
    service.setQuery(QStringLiteral("paint"));
    QVERIFY(service.busy());
    service.setQuery(QStringLiteral("terminal"));
    service.setQuery(QStringLiteral("huatu"));
    QVERIFY(service.results().isEmpty());
    QStringList publishedIds;
    connect(&service, &SearchService::resultsChanged, &service, [&] {
        publishedIds.append(serviceIds(service));
        QCOMPARE(QThread::currentThread(), QCoreApplication::instance()->thread());
    });
    QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 5000);
    QCOMPARE(service.query(), QStringLiteral("huatu"));
    QCOMPARE(serviceIds(service), QStringList{QStringLiteral("paint")});
    QCOMPARE(service.matchCount(), 1);
    QCOMPARE(publishedIds, QStringList{QStringLiteral("paint")});
    service.setQuery(QStringLiteral("not found"));
    QVERIFY(service.results().isEmpty());
    QCOMPARE(service.matchCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 5000);
    QVERIFY(service.results().isEmpty());
}

void SearchServiceTests::refreshedIndexCannotPublishRemovedTargets() {
    SearchService service;
    service.setEntries({{QStringLiteral("removed"), QStringLiteral("Application old"), {}}});
    service.setQuery(QStringLiteral("app"));
    service.setEntries({{QStringLiteral("replaced"), QStringLiteral("Application temporary"),
                         {}}});
    service.setEntries({{QStringLiteral("current"), QStringLiteral("Application current"), {}}});
    QStringList publishedIds;
    connect(&service, &SearchService::resultsChanged, &service,
            [&] { publishedIds.append(serviceIds(service)); });
    QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 5000);
    QCOMPARE(serviceIds(service), QStringList{QStringLiteral("current")});
    QCOMPARE(publishedIds, QStringList{QStringLiteral("current")});
    service.setEntries({});
    QVERIFY(service.results().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 5000);
    QCOMPARE(service.matchCount(), 0);
}

void SearchServiceTests::clearingQueryInvalidatesWorkAndDestructionJoins() {
    SearchService service;
    service.setEntries(referenceEntries());
    service.setQuery(QStringLiteral("chongqingyinhang"));
    service.setQuery({});
    QVERIFY(!service.busy());
    QVERIFY(service.results().isEmpty());
    // Synchronize with a new lookup after the cancelled job instead of sleeping.
    service.setQuery(QStringLiteral("paint"));
    QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 10000);
    QVERIFY(service.matchCount() > SearchIndex::maximumResults);
    QCOMPARE(service.results().size(), SearchIndex::maximumResults);
    service.setQuery(QStringLiteral(" \t　"));
    QVERIFY(!service.busy());
    QVERIFY(service.results().isEmpty());
    QCOMPARE(service.matchCount(), 0);
    for (int repetition = 0; repetition < 4; ++repetition) {
        auto pending = std::make_unique<SearchService>();
        pending->setEntries(referenceEntries());
        pending->setQuery(QStringLiteral("cqyh"));
        pending.reset();
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--search-benchmark"))) {
        int entryCount = 4096;
        if (arguments.size() != 2) {
            bool validCount = false;
            if (arguments.size() == 4 && arguments.at(2) == QLatin1String("--entries"))
                entryCount = arguments.at(3).toInt(&validCount);
            if (!validCount || entryCount < 0 || entryCount > xdstarter::SearchIndex::maximumEntries) {
                std::fprintf(stderr, "Use --search-benchmark [--entries 0..%d].\n",
                             xdstarter::SearchIndex::maximumEntries);
                return 2;
            }
        }
        return runReferenceBenchmark(entryCount);
    }
    SearchServiceTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "search_service_tests.moc"
