#include <QDir>
#include <QHash>
#include <QtDebug>
#include <QUrlQuery>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QScopedPointer>
#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QNetworkAccessManager>

#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#include <taglib/tpropertymap.h>

#include <gumbo-query/Document.h>
#include <gumbo-query/Node.h>

#include <Shlobj.h>

class PictureWrapper {
    TagLib::String mime_type;
    TagLib::FLAC::Picture::Type t;
    TagLib::ByteVector bv;
public:
    PictureWrapper(const TagLib::String &mime,
                   const TagLib::FLAC::Picture::Type &type,
                   const QByteArray &byte):
        mime_type(mime),
        t(type),
        bv(byte.data(), byte.length()) {}

    PictureWrapper(QNetworkReply *resp) {
        const QByteArray &bin = resp->readAll();
        bv = TagLib::ByteVector(bin.data(), bin.length());
        mime_type = resp->header(QNetworkRequest::ContentTypeHeader).toString().toStdString();
        t = TagLib::FLAC::Picture::FrontCover;
    }

    TagLib::FLAC::Picture *get_pic() {
        TagLib::FLAC::Picture *pic = new TagLib::FLAC::Picture();
        pic->setMimeType(mime_type);
        pic->setType(t);
        pic->setData(bv);
        return pic;
    }
};

const int bar_width = 50;
const QString app_name = "bp_audio";
const QString app_version = "1.0";

QCommandLineOption au_opt({"a", "au"}, "Audio AU number.", "au");
QCommandLineOption all_opt("menu", "Download whole menu.");
QCommandLineOption output_opt({"o", "out"}, "Output folder path.", "path");
QCommandLineParser parser;
QScopedPointer<QNetworkAccessManager> session;
QDir output;
QHash<QString, QSharedPointer<PictureWrapper> > assets;

QString find_audio(const QByteArray &data) {
    CDocument doc;
    doc.parse(data.toStdString());
    CSelection audio = doc.find("audio");
    return QString::fromStdString(audio.nodeAt(0).attribute("src"));
}

void download_progress(const qint64 &bytes_received, const qint64 &bytes_total) {
    if (bytes_total == -1) return;
    std::cerr << '[';
    int pos = bar_width * bytes_received / bytes_total;
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) std::cerr << '=';
        else if (i == pos) std::cerr << '>';
        else std::cerr << ' ';
    }
    std::cerr << "] " << int(100 * bytes_received / bytes_total) << " %\r";
    if (bytes_received >= bytes_total) std::cerr << '\n';
}

void write_metadata(const QString &file, const QJsonObject &data,
                    const int &track, const int &total) {
    TagLib::FLAC::File f(TagLib::FileName(file.toStdWString().data()));
    if (!f.isValid()) {
        qCritical("Downloaded song not valid");
        std::exit(EXIT_FAILURE);
    }

    TagLib::PropertyMap map;
    map.insert("TITLE", TagLib::String(data["title"].toString().toStdWString()));
    map.insert("ARTIST", TagLib::String(data["author"].toString().toStdWString()));
    const auto &album_info = data.find("pgc_info");
    if (album_info != data.end()) {
        const QJsonObject &album_info_obj = album_info->toObject();
        if (album_info_obj.find("pgc_menu") != album_info_obj.end()) {
            const QJsonObject &album_menu = album_info_obj["pgc_menu"].toObject();
            map.insert("ALBUMARTIST", TagLib::String(album_menu["mbnames"].toString().toStdWString()));
            map.insert("ALBUM", TagLib::String(album_menu["title"].toString().toStdWString()));
            map.insert("DATE", TagLib::String(QDateTime::fromTime_t(album_menu["pubTime"].toInt()).toString("yyyy-MM-dd").toStdWString()));
            map.insert("LABEL", TagLib::String(album_menu["publisher"].toString().toStdWString()));
            map.insert("DISCNUMBER", TagLib::String(QString::number(1).toStdWString()));
            map.insert("DISCTOTAL", TagLib::String(QString::number(1).toStdWString()));
        }
    }
    if (track != -1 && total != -1) {
        map.insert("TRACKNUMBER", TagLib::String(QString::number(track).toStdWString()));
        map.insert("TRACKTOTAL", TagLib::String(QString::number(total).toStdWString()));
    }
    f.xiphComment(true)->setProperties(map);

    const QString &cover_url = data["cover_url"].toString();

    QSharedPointer<PictureWrapper> w;
    if (assets.count(cover_url)) {
        w = assets[cover_url];
    } else {
        QNetworkRequest cover_req(QUrl(data["cover_url"].toString()));
        QScopedPointer<QNetworkReply> cover_resp(session->get(cover_req));
        qInfo("Getting album art...");
        QEventLoop loop;
        QObject::connect(cover_resp.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (cover_resp->error() != QNetworkReply::NoError) {
            qCritical("Error loading album art");
            qDebug("  %s", qUtf8Printable(cover_resp->errorString()));
            std::exit(EXIT_FAILURE);
        }
        w.reset(new PictureWrapper(cover_resp.data()));
        assets.insert(cover_url, w);
    }

    f.addPicture(w->get_pic());
    f.save();
}

void download_song(const QJsonObject &data, const int &track = -1, const int &total = -1) {
    QString file_name = QStringLiteral("%1 - %2.flac").arg(data["author"].toString(), data["title"].toString());
    std::array<wchar_t, MAX_PATH> buf{};
    file_name.toWCharArray(buf.data());
    PathCleanupSpec(output.absolutePath().toStdWString().data(), buf.data());
    file_name = QString::fromWCharArray(buf.data());

    const QString &au = QString::number(data["id"].toInt());
    qInfo("Getting audio preview for %s...", qUtf8Printable(au));
    QNetworkRequest song_link_req(QUrl("https://m.bilibili.com/audio/au" + au));
    QScopedPointer<QNetworkReply> link_page(session->get(song_link_req));
    QEventLoop loop;
    QObject::connect(link_page.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (link_page->error() != QNetworkReply::NoError) {
        qCritical("Error loading audio preview");
        qDebug("  %s", qUtf8Printable(link_page->errorString()));
        std::exit(EXIT_FAILURE);
    }

    const QString &audio_src = find_audio(link_page->readAll());
    QUrl audio_url(audio_src);

    QString audio_path = audio_url.path();
    audio_path.replace("preview", "flac");
    const int &suffix_length = QFileInfo(audio_path).suffix().length();
    audio_path.replace(audio_path.length() - suffix_length, suffix_length, "flac");

    audio_url.setPath(audio_path);
    audio_url.setQuery(QUrlQuery());

    QNetworkRequest song_flac(audio_url);
    QScopedPointer<QNetworkReply> song_res(session->get(song_flac));
    QObject::connect(song_res.data(), &QNetworkReply::downloadProgress, download_progress);
    QObject::connect(song_res.data(), &QNetworkReply::readyRead, &loop, &QEventLoop::quit);
    QObject::connect(song_res.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);

    file_name = output.filePath(file_name);
    qInfo("Writing to %s...", qUtf8Printable(file_name));
    QFile f(file_name);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        do {
            loop.exec();
            if (song_res->error() != QNetworkReply::NoError) {
                qCritical("Error loading audio resource");
                qDebug("  %s", qUtf8Printable(song_res->errorString()));
                std::exit(EXIT_FAILURE);
            }
            f.write(song_res->readAll());
        } while (!song_res->isFinished());
        f.close();
    }
    write_metadata(file_name, data, track, total);
    qInfo("Finished for %s", qUtf8Printable(file_name));
}

void get_menu_info(const QString &menuid) {
    QNetworkRequest menu_info_req(QUrl("https://www.bilibili.com/audio/music-service-c/menus/" + menuid));
    QScopedPointer<QNetworkReply> info(session->get(menu_info_req));

    qInfo("Start loading menu info for %s...", qUtf8Printable(menuid));
    QEventLoop loop;
    QObject::connect(info.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (info->error() != QNetworkReply::NoError) {
        qCritical("Error loading menu info");
        qDebug("  %s", qUtf8Printable(info->errorString()));
        std::exit(EXIT_FAILURE);
    }

    const QJsonDocument &doc = QJsonDocument::fromJson(info->readAll());
    const QJsonObject &obj = doc.object();

    if (obj["code"].toInt() != 0) {
        qCritical("Error loading menu info");
        qDebug("  %d: %s", obj["code"].toInt(), qUtf8Printable(obj["msg"].toString()));
        std::exit(EXIT_FAILURE);
    }

    const QJsonObject &data = obj["data"].toObject();

    const QJsonObject &menus_response = data["menusRespones"].toObject();
    QString folder_name = QStringLiteral("%1 - %2").arg(menus_response["mbnames"].toString(), menus_response["title"].toString());
    std::array<wchar_t, MAX_PATH> buf{};
    folder_name.toWCharArray(buf.data());
    PathCleanupSpec(output.absolutePath().toStdWString().data(), buf.data());
    folder_name = QString::fromWCharArray(buf.data());
    output.mkdir(folder_name);
    output.cd(folder_name);

    const QJsonArray &songsList = data["songsList"].toArray();
    int counter = 0;
    const int &total = songsList.size();
    for (const auto &&i : songsList) {
        download_song(i.toObject(), ++counter, total);
    }
}

void get_song_info(const QString &au)
{
    QNetworkRequest song_info_req(QUrl("https://www.bilibili.com/audio/music-service-c/songs/playing?song_id=" + au));
    QScopedPointer<QNetworkReply> info(session->get(song_info_req));

    qInfo("Start loading song info for %s...", qUtf8Printable(au));
    QEventLoop loop;
    QObject::connect(info.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (info->error() != QNetworkReply::NoError) {
        qCritical("Error loading song info");
        qDebug("  %s", qUtf8Printable(info->errorString()));
        std::exit(EXIT_FAILURE);
    }

    const QJsonDocument &doc = QJsonDocument::fromJson(info->readAll());
    const QJsonObject &obj = doc.object();
    if (obj["code"].toInt() != 0) {
        qCritical("Error loading song info");
        qDebug("  %d: %s", obj["code"].toInt(), qUtf8Printable(obj["msg"].toString()));
        std::exit(EXIT_FAILURE);
    }

    const QJsonObject &data = obj["data"].toObject();
    if (parser.isSet(all_opt)) {
        const auto &album_info = data.find("pgc_info");
        if (album_info != data.end()) {
            const QJsonObject &album_info_obj = album_info->toObject();
            if (album_info_obj.find("pgc_menu") != album_info_obj.end()) {
                const QJsonObject &album_menu = album_info_obj["pgc_menu"].toObject();
                get_menu_info(QString::number(album_menu["menuId"].toInt()));
                return;
            }
        }
        qDebug("No menu found.");
    }
    download_song(data);
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    a.setApplicationName(app_name);
    a.setApplicationVersion(app_version);

    session.reset(new QNetworkAccessManager());

    parser.addOption(au_opt);
    parser.addOption(all_opt);
    parser.addOption(output_opt);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.setApplicationDescription("Get to know some free official FLAC music.");
    parser.process(a);

    const QString &au_number = parser.value(au_opt);
    QString output_path = parser.value(output_opt);
    output.setPath(output_path);
    if (au_number.isEmpty()) {
        qWarning("\nAU number is required.\n");
        parser.showHelp();
    } else {
        bool ok = false;
        au_number.toInt(&ok, 10);
        if (!ok) {
            qWarning("\nAU needs to be a number.\n");
            parser.showHelp();
        }
    }
    if (output_path.isEmpty()) {
        output = QDir::current();
    } else if (!output.exists()) {
        qWarning("\nOutput path does not exist.\n");
        parser.showHelp();
    }

    get_song_info(au_number);

    return EXIT_SUCCESS;
}
