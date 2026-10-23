// tests/services/application_browser_tests.cpp

#include "services/application_catalog.h"

#include <QtTest>

namespace {
xdstarter::LaunchTarget entry(const QString& name, const QString& path,
                              const QString& type = QStringLiteral("shortcut"),
                              const QString& parent = {}) {
    return {xdstarter::applicationTargetId(type, path),
            type,
            name,
            name.first(1),
            path,
            {},
            {},
            parent};
}
} // namespace

class ApplicationBrowserTest final : public QObject {
    Q_OBJECT
  private slots:
    void sections_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");
        QTest::newRow("digits") << QStringLiteral("123 应用") << QStringLiteral("#");
        QTest::newRow("ascii") << QStringLiteral("browser") << QStringLiteral("B");
        QTest::newRow("accent") << QStringLiteral("Éditeur") << QStringLiteral("E");
        QTest::newRow("chinese") << QStringLiteral("微信") << QStringLiteral("W");
        QTest::newRow("trim") << QStringLiteral("  画图") << QStringLiteral("H");
        QTest::newRow("symbol") << QStringLiteral("★工具") << QStringLiteral("🌐");
        QTest::newRow("surrogate") << QString::fromUtf8("🛠 Toolbox") << QStringLiteral("🌐");
        QTest::newRow("empty") << QString{} << QStringLiteral("🌐");
    }
    void sections() {
        QFETCH(QString, name);
        QFETCH(QString, expected);
        QCOMPARE(xdstarter::applicationSection(name), expected);
    }
    void hierarchyAndRefresh() {
        using namespace xdstarter;
        const auto folder = entry(QStringLiteral("工具"), QStringLiteral("C:/Fixture/Tools"),
                                  QStringLiteral("folder"));
        const auto nested = entry(QStringLiteral("开发"), QStringLiteral("C:/Fixture/Tools/Dev"),
                                  QStringLiteral("folder"), folder.id);
        const auto child =
            entry(QStringLiteral("画图"), QStringLiteral("C:/Fixture/Tools/Paint.lnk"),
                  QStringLiteral("shortcut"), folder.id);
        const auto leaf =
            entry(QStringLiteral("Terminal"), QStringLiteral("C:/Fixture/Tools/Dev/Terminal.lnk"),
                  QStringLiteral("shortcut"), nested.id);
        ApplicationListModel source;
        ApplicationBrowserModel browser;
        browser.setSourceModel(&source);
        browser.sort(0);
        source.replace(normalizedApplications(
            {entry(QStringLiteral("Zeta"), QStringLiteral("C:/Fixture/Z.lnk")), folder, nested,
             child, leaf, entry(QStringLiteral("1App"), QStringLiteral("C:/Fixture/1.lnk")),
             entry(QStringLiteral("★未知"), QStringLiteral("C:/Fixture/Other.lnk"))}));
        QCOMPARE(browser.rowCount(), 4);
        QCOMPARE(browser.sections(), (QStringList{QStringLiteral("#"), QStringLiteral("G"),
                                                  QStringLiteral("Z"), QStringLiteral("🌐")}));
        QCOMPARE(browser.sectionIndex(QStringLiteral("Z")), 2);
        QCOMPARE(browser.sectionIndex(QStringLiteral("A")), -1);
        QVERIFY(!browser.openFolder(leaf.id));
        QVERIFY(browser.openFolder(folder.id));
        QVERIFY(browser.inFolder());
        QCOMPARE(browser.folderName(), folder.name);
        QCOMPARE(browser.rowCount(), 2);
        QVERIFY(browser.openFolder(nested.id));
        QCOMPARE(browser.rowCount(), 1);
        QCOMPARE(browser.index(0, 0).data(ApplicationListModel::AppId).toString(), leaf.id);
        browser.back();
        QCOMPARE(browser.rowCount(), 2);
        source.replace({});
        browser.reconcile();
        QVERIFY(!browser.inFolder());
        QCOMPARE(browser.rowCount(), 0);
        QVERIFY(!isValidApplicationTarget(folder));
        QVERIFY(isValidCatalogEntry(folder));
    }
};
QTEST_GUILESS_MAIN(ApplicationBrowserTest)
#include "application_browser_tests.moc"
