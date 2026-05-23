#include <QtTest/QtTest>
#include "MarshallCv370Controller.h"

class MarshallCv370ControllerTest : public QObject {
    Q_OBJECT

private slots:
    void setNightModeBuildsDaylightRequest();
    void setNightModeBuildsNightRequest();
    void hostNormalizationAcceptsSchemeAndTrailingSlash();
    void extractsHostFromNdiUrlAddress();
    void buildsDetectionRequest();
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

void MarshallCv370ControllerTest::extractsHostFromNdiUrlAddress() {
    QCOMPARE(MarshallCv370Controller::hostFromNdiUrlAddress("192.168.10.42:5961"),
             QStringLiteral("192.168.10.42"));
    QCOMPARE(MarshallCv370Controller::hostFromNdiUrlAddress("http://cv370.local:5961/path"),
             QStringLiteral("cv370.local"));
}

void MarshallCv370ControllerTest::buildsDetectionRequest() {
    const QUrl url = MarshallCv370Controller::buildDetectUrl("192.168.10.42:5961");

    QCOMPARE(url.toString(), QStringLiteral("http://192.168.10.42/cgi-bin/web.fcgi?func=get%7B%22image%22:[%22ircut%22]%7D"));
}

QTEST_MAIN(MarshallCv370ControllerTest)
#include "test_marshall_cv370_controller.moc"
