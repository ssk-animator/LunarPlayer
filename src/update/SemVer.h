#ifndef SEMVER_H
#define SEMVER_H

#include <QString>
#include <QRegularExpression>

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString prerelease;

    SemVer() = default;
    SemVer(int maj, int min, int pat, const QString &pr = {})
        : major(maj), minor(min), patch(pat), prerelease(pr) {}

    static SemVer fromString(const QString &str, bool *ok = nullptr) {
        static QRegularExpression re(
            R"(^v?(\d+)\.(\d+)\.(\d+)(?:-([a-zA-Z0-9]+(?:\.[a-zA-Z0-9]+)*))?)");
        auto m = re.match(str.trimmed());
        if (!m.hasMatch()) {
            if (ok) *ok = false;
            return {};
        }
        if (ok) *ok = true;
        return {m.captured(1).toInt(), m.captured(2).toInt(),
                m.captured(3).toInt(), m.captured(4)};
    }

    QString toString() const {
        QString v = QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
        if (!prerelease.isEmpty()) v += "-" + prerelease;
        return v;
    }

    bool isValid() const { return major > 0 || minor > 0 || patch > 0; }

    bool operator>(const SemVer &o) const {
        if (major != o.major) return major > o.major;
        if (minor != o.minor) return minor > o.minor;
        if (patch != o.patch) return patch > o.patch;
        if (prerelease.isEmpty() && !o.prerelease.isEmpty()) return true;
        if (!prerelease.isEmpty() && o.prerelease.isEmpty()) return false;
        return prerelease > o.prerelease;
    }

    bool operator<(const SemVer &o) const { return o > *this; }
    bool operator==(const SemVer &o) const {
        return major == o.major && minor == o.minor && patch == o.patch
               && prerelease == o.prerelease;
    }
    bool operator!=(const SemVer &o) const { return !(*this == o); }
    bool operator>=(const SemVer &o) const { return *this > o || *this == o; }
    bool operator<=(const SemVer &o) const { return *this < o || *this == o; }
};

#endif
