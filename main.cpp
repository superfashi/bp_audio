#include <QDir>
#include <QtDebug>
#include <QUrlQuery>
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

QCommandLineOption au_opt({"a", "au"}, "Audio AU number.", "au");
QCommandLineOption all_opt("menu", "Download whole menu.");
QCommandLineOption output_opt({"o", "out"}, "Output folder path.", "path");
QCommandLineParser parser;
QScopedPointer<QNetworkAccessManager> session;
QDir output;

QString find_audio(const QByteArray &data) {
    CDocument doc;
    doc.parse(data.toStdString());
    CSelection audio = doc.find("audio");
    return QString::fromStdString(audio.nodeAt(0).attribute("src"));
}

void write_metadata(const QString &file, const QJsonObject &data) {
    TagLib::FLAC::File f(qPrintable(file));
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
        }
    }
    f.xiphComment(true)->setProperties(map);

    QNetworkRequest cover_req(QUrl(data["cover_url"].toString()));
    QScopedPointer<QNetworkReply> cover_resp(session->get(cover_req));
    qInfo("Getting album art...");
    QEventLoop loop;
    QObject::connect(cover_resp.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray &cover_bin = cover_resp->readAll();

    TagLib::ByteVector bv(cover_bin.data(), cover_bin.length());
    TagLib::FLAC::Picture pic;
    pic.setMimeType(cover_resp->header(QNetworkRequest::ContentTypeHeader).toString().toStdString());
    pic.setType(TagLib::FLAC::Picture::FrontCover);
    pic.setData(bv);
    f.addPicture(&pic);

    f.save();
}

void get_song_info(const QString &au)
{
    QNetworkRequest song_info_req(QUrl("https://www.bilibili.com/audio/music-service-c/songs/playing?song_id=" + au));
    QScopedPointer<QNetworkReply> info(session->get(song_info_req));

    qInfo("Start loading song info for %s...", qUtf8Printable(au));
    QEventLoop loop;
    QObject::connect(info.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QJsonDocument &doc = QJsonDocument::fromJson(info->readAll());
    const QJsonObject &obj = doc.object();
    if (obj["code"].toInt() != 0) {
        qCritical("Error loading song info");
        qDebug("  %d: %s", obj["code"].toInt(), qUtf8Printable(obj["msg"].toString()));
        std::exit(EXIT_FAILURE);
    }

    const QJsonObject &data = obj["data"].toObject();
    QString file_name = QStringLiteral("%1 - %2.flac").arg(data["author"].toString(), data["title"].toString());
    std::array<wchar_t, MAX_PATH> buf{};
    file_name.toWCharArray(buf.data());
    PathCleanupSpec(NULL, buf.data());
    file_name = QString::fromWCharArray(buf.data());

    qInfo("Getting audio preview for %s...", qUtf8Printable(au));
    QNetworkRequest song_link_req(QUrl("https://m.bilibili.com/audio/au" + au));
    QScopedPointer<QNetworkReply> link_page(session->get(song_link_req));
    QObject::connect(link_page.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

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
    QObject::connect(song_res.data(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    qInfo("Writing to %s...", qUtf8Printable(file_name));
    file_name = output.filePath(file_name);
    QFile f(file_name);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(song_res->readAll());
        f.close();
    }
    write_metadata(file_name, data);
    qInfo("Finished for %s...", qUtf8Printable(file_name));
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    session.reset(new QNetworkAccessManager());

    parser.addOption(au_opt);
    parser.addOption(all_opt);
    parser.addOption(output_opt);
    parser.addHelpOption();
    parser.setApplicationDescription("Free bili lossless audio.");
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
