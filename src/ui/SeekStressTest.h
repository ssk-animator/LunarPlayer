#ifndef SEEKSTRESSTEST_H
#define SEEKSTRESSTEST_H

#include <QObject>
#include <QElapsedTimer>
#include <functional>
#include <vector>
#include <string>
#include <cstdio>

class SeekStressTest : public QObject {
    Q_OBJECT
public:
    using SeekFunc = std::function<void(double)>;
    using VerifyFunc = std::function<bool(double &)>;
    using WaitFunc = std::function<void(int ms)>;

    explicit SeekStressTest(QObject *parent = nullptr) : QObject(parent) {}

    struct TestResult {
        int totalSeeks = 0;
        int passed = 0;
        int failed = 0;
        std::string failureReason;
        double totalMs = 0.0;
    };

    void setSeekFunc(SeekFunc fn) { m_seekFunc = fn; }
    void setVerifyFunc(VerifyFunc fn) { m_verifyFunc = fn; }
    void setWaitFunc(WaitFunc fn) { m_waitFunc = fn; }

    TestResult run(double durationSec, int seekCount = 20) {
        TestResult result;
        QElapsedTimer totalTimer;
        totalTimer.start();

        if (!m_seekFunc || !m_verifyFunc || !m_waitFunc) {
            result.failureReason = "Functions not set";
            result.failed = 1;
            result.totalSeeks = 1;
            return result;
        }

        fprintf(stderr, "\n[SEEK-TEST] === START: %d seeks over %.0fs duration ===\n", seekCount, durationSec);
        fflush(stderr);

        std::vector<double> targets;
        for (int i = 0; i < seekCount; i++) {
            double t = (durationSec * (i + 1)) / (seekCount + 1);
            targets.push_back(t);
        }

        for (int i = 0; i < seekCount; i++) {
            double target = targets[i];
            result.totalSeeks++;

            fprintf(stderr, "[SEEK-TEST] Seek %d/%d to %.1fs\n", i + 1, seekCount, target);
            fflush(stderr);

            m_seekFunc(target);
            m_waitFunc(500);

            double errorMs = 0.0;
            bool ok = m_verifyFunc(errorMs);

            if (ok) {
                result.passed++;
                fprintf(stderr, "[SEEK-TEST] Seek %d/%d PASS (error=%.1fms)\n", i + 1, seekCount, errorMs);
            } else {
                result.failed++;
                result.failureReason = "Seek " + std::to_string(i + 1) + " failed verification";
                fprintf(stderr, "[SEEK-TEST] Seek %d/%d FAIL\n", i + 1, seekCount);
            }
            fflush(stderr);
        }

        result.totalMs = totalTimer.nsecsElapsed() / 1000000.0;

        fprintf(stderr, "\n[SEEK-TEST] === RESULT: %d/%d passed in %.0fms ===\n",
                result.passed, result.totalSeeks, result.totalMs);
        if (result.failed == 0) {
            fprintf(stderr, "[SEEK-TEST] PASS\n");
        } else {
            fprintf(stderr, "[SEEK-TEST] FAIL: %s\n", result.failureReason.c_str());
        }
        fflush(stderr);

        return result;
    }

private:
    SeekFunc m_seekFunc;
    VerifyFunc m_verifyFunc;
    WaitFunc m_waitFunc;
};

#endif
