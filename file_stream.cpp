#include <QFile>
#include <taglib/tiostream.h>

class FileStream: public TagLib::IOStream {
    QFile *file;
    unsigned int bufferSize() const { return 1024; }
public:
    FileStream(QFile *f): file(f) {}

    bool readOnly() const {
        return !file->isWritable();
    }

    bool isOpen() const {
        return file->isOpen();
    }

    long tell() const {
        return file->pos();
    }

    long length() {
        return file->size();
    }

    void truncate(long length) {
        file->resize(length);
    }

    void seek(long offset,
              TagLib::IOStream::Position p = TagLib::IOStream::Beginning) {
        switch (p) {
        case TagLib::IOStream::Beginning:
            file->seek(offset);
            break;
        case TagLib::IOStream::Current:
            file->seek(file->pos() + offset);
            break;
        case TagLib::IOStream::End:
            file->seek(file->size() + offset);
        }
    }

    TagLib::FileName name() const {
        return file->fileName().toStdWString().data();
    }

    TagLib::ByteVector readBlock(unsigned long length) {
        TagLib::ByteVector bv(static_cast<unsigned int>(length));
        const qint64 &l = file->read(bv.data(), length);
        bv.resize(l);
        return std::move(bv);
    }

    void writeBlock(const TagLib::ByteVector &data) {
        file->write(data.data(), data.size());
    }

    void insert(const TagLib::ByteVector &data,
                unsigned long start,
                unsigned long replace) {
        if (data.size() == replace) {
            seek(start);
            writeBlock(data);
            return;
        }
        if (data.size() < replace) {
            seek(start);
            writeBlock(data);
            removeBlock(start + data.size(), replace - data.size());
            return;
        }
        unsigned long bufferLength = bufferSize();
        while (data.size() - replace > bufferLength)
            bufferLength += bufferSize();

        long readPosition = start + replace;
        long writePosition = start;

        QByteArray buffer(data.data(), data.size());
        QByteArray aboutToOverwrite;
        aboutToOverwrite.resize(bufferLength);

        while (true) {
            seek(readPosition);
            const qint64 &bytesRead = file->peek(aboutToOverwrite.data(), bufferLength);
            aboutToOverwrite.resize(bytesRead);
            readPosition += bufferLength;

            if (bytesRead < bufferLength) clear();

            seek(writePosition);
            file->write(buffer);

            if (bytesRead == 0) break;

            writePosition += buffer.size();
            buffer = aboutToOverwrite;
        }
    }

    void removeBlock(unsigned long start, unsigned long length) {
        unsigned long bufferLength = bufferSize();

        long readPosition = start + length;
        long writePosition = start;

        QByteArray buffer;
        buffer.resize(bufferLength);

        for (qint64 bytesRead = -1; bytesRead != 0;)
        {
            seek(readPosition);
            bytesRead = file->peek(buffer.data(), bufferLength);
            readPosition += bytesRead;

            if (bytesRead < buffer.size()) {
                clear();
                buffer.resize(bytesRead);
            }

            seek(writePosition);
            file->write(buffer);

            writePosition += bytesRead;
        }

        truncate(writePosition);
    }
};
