#include <QtTest/QtTest>
#include "MarshallCv370Controller.h"

class MarshallCv370ControllerTest : public QObject {
    Q_OBJECT

private slots:
    void setNightModeBuildsDaylightRequest();
    void setNightModeBuildsNightRequest();
    void hostNormalizationAcceptsSchemeAndTrailingSlash();
};

void MarshallCv370ControllerTest::setNightModeBuildsDaylightRequest() {
    const QUrl url = MarshallCv370Controller::buildSetIrCutUrl("192.168.10.42", false);

    QCOMPARE(url.scheme(), QStringLiteral("http"));
    QCOMPARE(url.host(), QStringLiteral("192.168.10.42"));
    QCOMPARE(url.path(), QStringLiteral("/cgi-bin/web.fcgi"));
    QCOMPARE(url.query(QUrl::FullyEncoded), QStringLiteral("func=set%7B%22image%22:%7B%22ircut%22:1%7D%7D"));
}

void MarshallCv370ControllerTest::setNightModeBuildsNightRequest() {
    const QUrl url = MarshallCv370Controller::buildSetIrCutUrl("cv370.local", true);

    QCOMPARE(url.toString(), QStringLiteral("http://cv370.local/cgi-bin/web.fcgi?func=set%7B%22image%22:%7B%22ircut%22:0%7D%7D"));
}

void MarshallCv370ControllerTest::hostNormalizationAcceptsSchemeAndTrailingSlash() {
    const QUrl url = MarshallCv370Controller::buildSetIrCutUrl("http://cv370.local/", false);

    QCOMPARE(url.toString(), QStringLiteral("http://cv370.local/cgi-bin/web.fcgi?func=set%7B%22image%22:%7B%22ircut%22:1%7D%7D"));
}

QTEST_MAIN(MarshallCv370ControllerTest)
#include "test_marshall_cv370_controller.moc"
