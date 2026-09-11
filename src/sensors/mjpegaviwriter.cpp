#include "sensors/mjpegaviwriter.h"
#include <QDataStream>
#include <utility>

namespace {
void writeFourCc(QDataStream &stream, const char value[5])
{
    stream.writeRawData(value, 4);
}
}

bool MjpegAviWriter::open(const QString &path, int width, int height)
{
    m_recordingFile.setFileName(path);
    if (!m_recordingFile.open(QIODevice::WriteOnly)) return false;
    m_recordingWidth = width;
    m_recordingHeight = height;
    m_recordingFrames.clear();
    QDataStream out(&m_recordingFile);
    out.setByteOrder(QDataStream::LittleEndian);
    writeFourCc(out, "RIFF"); m_recordingRiffSizeOffset = m_recordingFile.pos(); out << quint32(0);
    writeFourCc(out, "AVI ");
    writeFourCc(out, "LIST"); out << quint32(192); writeFourCc(out, "hdrl");
    writeFourCc(out, "avih"); out << quint32(56) << quint32(100000) << quint32(0)
        << quint32(0) << quint32(0x10);
    m_recordingFrameCountOffset = m_recordingFile.pos(); out << quint32(0);
    out << quint32(0) << quint32(1) << quint32(0) << quint32(m_recordingWidth)
        << quint32(m_recordingHeight) << quint32(0) << quint32(0) << quint32(0) << quint32(0);
    writeFourCc(out, "LIST"); out << quint32(116); writeFourCc(out, "strl");
    writeFourCc(out, "strh"); out << quint32(56); writeFourCc(out, "vids"); writeFourCc(out, "MJPG");
    out << quint32(0) << quint32(0) << quint32(0) << quint32(1) << quint32(10) << quint32(0);
    m_recordingStreamLengthOffset = m_recordingFile.pos(); out << quint32(0);
    out << quint32(0) << quint32(0xFFFFFFFF) << quint32(0)
        << qint16(0) << qint16(0) << qint16(m_recordingWidth) << qint16(m_recordingHeight);
    writeFourCc(out, "strf"); out << quint32(40) << quint32(40) << qint32(m_recordingWidth)
        << qint32(m_recordingHeight) << quint16(1) << quint16(24); writeFourCc(out, "MJPG");
    out << quint32(m_recordingWidth * m_recordingHeight * 3) << qint32(0) << qint32(0)
        << quint32(0) << quint32(0);
    writeFourCc(out, "LIST"); m_recordingMoviSizeOffset = m_recordingFile.pos(); out << quint32(0);
    writeFourCc(out, "movi"); m_recordingMoviDataOffset = m_recordingFile.pos();
    return true;
}

void MjpegAviWriter::append(const QByteArray &jpeg, int width, int height)
{
    if (!m_recordingFile.isOpen() || width != m_recordingWidth || height != m_recordingHeight) return;
    const qint64 position = m_recordingFile.pos();
    QDataStream out(&m_recordingFile);
    out.setByteOrder(QDataStream::LittleEndian);
    writeFourCc(out, "00dc"); out << quint32(jpeg.size());
    m_recordingFile.write(jpeg);
    if (jpeg.size() & 1) m_recordingFile.putChar('\0');
    m_recordingFrames.append({static_cast<quint32>(position - m_recordingMoviDataOffset),
                              static_cast<quint32>(jpeg.size())});
}

bool MjpegAviWriter::finish()
{
    if (!m_recordingFile.isOpen()) return false;
    QDataStream out(&m_recordingFile);
    out.setByteOrder(QDataStream::LittleEndian);
    const qint64 indexPosition = m_recordingFile.pos();
    writeFourCc(out, "idx1"); out << quint32(m_recordingFrames.size() * 16);
    for (const AviFrame &frame : std::as_const(m_recordingFrames)) {
        writeFourCc(out, "00dc"); out << quint32(0x10) << frame.offset << frame.size;
    }
    const qint64 finalSize = m_recordingFile.pos();
    m_recordingFile.seek(m_recordingRiffSizeOffset); out << quint32(finalSize - 8);
    m_recordingFile.seek(m_recordingFrameCountOffset); out << quint32(m_recordingFrames.size());
    m_recordingFile.seek(m_recordingStreamLengthOffset); out << quint32(m_recordingFrames.size());
    m_recordingFile.seek(m_recordingMoviSizeOffset);
    out << quint32(indexPosition - (m_recordingMoviSizeOffset + 4));
    m_recordingFile.close();
    return hasFrames();
}
