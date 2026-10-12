// SPDX-License-Identifier: GPL-3.0-or-later
// qtshim.h -- a tiny std-only stand-in for the handful of Qt 4 classes (QString, QList, QLatin1String,
// QRegExp, ...) that MeeTube's Qt-free JSON parsers touch, so they can be compiled without Qt.
// UTF-8 bytes live in std::string.  Only what the vendored parsers need; NOT a general Qt replacement.
#pragma once
#include <string>
#include <vector>
#include <regex>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
typedef long long qint64;
typedef unsigned char uchar;
#define Q_DECLARE_METATYPE(x)
#define Q_OBJECT
#ifndef QT_VERSION_CHECK
#endif
struct QLatin1String { const char *s; explicit QLatin1String(const char *p):s(p){} };
struct QLatin1Char { char c; explicit QLatin1Char(char x):c(x){} };
namespace Qt { enum CaseSensitivity { CaseInsensitive=0, CaseSensitive=1 }; }
class QRegExp {
public:
    std::regex re; std::string pat;
    QRegExp(const char *p):re(p),pat(p){}
    QRegExp(const QLatin1String &l):re(l.s),pat(l.s){}
    template<class S> bool exactMatch(const S &s) const { return std::regex_match(s.s_, re); }
    template<class S> int indexIn(const S &s) const { std::smatch m; if(std::regex_search(s.s_,m,re)) return (int)m.position(0); return -1; }
};
class QString {
public:
    std::string s_;
    QString(){}
    QString(const char *p):s_(p?p:""){}
    QString(const QLatin1String &l):s_(l.s){}
    QString(const std::string &x):s_(x){}
    static QString fromUtf8(const char *p,int n=-1){ return n<0?QString(p):QString(std::string(p,n)); }
    static QString fromLatin1(const char *p,int n=-1){ return fromUtf8(p,n); }
    static QString number(long long v){ return QString(std::to_string(v)); }
    static QString number(long v){ return QString(std::to_string(v)); }
    static QString number(int v){ return QString(std::to_string(v)); }
    bool isEmpty() const { return s_.empty(); }
    bool isNull() const { return s_.empty(); }
    int size() const { return (int)s_.size(); }
    int length() const { return (int)s_.size(); }
    bool contains(const QRegExp &r) const { return std::regex_search(s_, r.re); }
    bool contains(const QLatin1String &l) const { return s_.find(l.s)!=std::string::npos; }
    bool contains(const char *l) const { return s_.find(l)!=std::string::npos; }
    bool startsWith(const QLatin1String &l) const { return s_.rfind(l.s,0)==0; }
    bool startsWith(const char *l) const { return s_.rfind(l,0)==0; }
    bool startsWith(const QLatin1Char &c) const { return !s_.empty() && s_[0]==c.c; }
    bool contains(const char *l, Qt::CaseSensitivity cs) const { if(cs==Qt::CaseSensitive) return contains(l); std::string a=s_, b=l; for(auto&ch:a) ch=(char)tolower((unsigned char)ch); for(auto&ch:b) ch=(char)tolower((unsigned char)ch); return a.find(b)!=std::string::npos; }
    QString section(const QLatin1Char &c,int a,int b) const { return section(QString(std::string(1,c.c)),a,b); }
    QString &prepend(const QString &o){ s_=o.s_+s_; return *this; }
    QString &prepend(const QLatin1String &o){ s_=std::string(o.s)+s_; return *this; }
    QString &append(const QString &o){ s_+=o.s_; return *this; }
    QString &remove(const QRegExp &r){ s_=std::regex_replace(s_,r.re,""); return *this; }
    qint64 toLongLong() const { return std::strtoll(s_.c_str(),nullptr,10); }
    int toInt() const { return (int)std::strtol(s_.c_str(),nullptr,10); }
    QString section(const QString &sep,int a,int b) const { /* only [a,b] with a==b used */ size_t p=0; int i=0; std::string cur; while(true){ size_t q=s_.find(sep.s_,p); std::string part=s_.substr(p,q==std::string::npos?std::string::npos:q-p); if(i>=a&&i<=b) cur+= (cur.empty()?"":sep.s_)+part; if(q==std::string::npos) break; p=q+sep.s_.size(); ++i; } return QString(cur); }
    QString section(const char *sep,int a,int b) const { return section(QString(sep),a,b); }
    QString trimmed() const { size_t a=s_.find_first_not_of(" \t\r\n"), b=s_.find_last_not_of(" \t\r\n"); return a==std::string::npos?QString():QString(s_.substr(a,b-a+1)); }
    bool operator==(const QString &o) const { return s_==o.s_; }
    bool operator!=(const QString &o) const { return s_!=o.s_; }
    bool operator==(const QLatin1String &o) const { return s_==o.s; }
    bool operator!=(const QLatin1String &o) const { return s_!=o.s; }
    bool operator==(const char *o) const { return s_==o; }
    QString operator+(const QString &o) const { return QString(s_+o.s_); }
    QString &operator+=(const QString &o){ s_+=o.s_; return *this; }
    const char *toUtf8() const { return s_.c_str(); }
    std::string toStdString() const { return s_; }
};
inline QString operator+(const char *a,const QString &b){ return QString(std::string(a)+b.s_); }
template<class T> class QList : public std::vector<T> {
public:
    QList &operator<<(const T &v){ this->push_back(v); return *this; }
    void append(const T &v){ this->push_back(v); }
    bool isEmpty() const { return this->empty(); }
    int count() const { return (int)this->size(); }
    const T &at(int i) const { return (*this)[i]; }
    T &first(){ return this->front(); }
};
class QStringList : public QList<QString> {};
