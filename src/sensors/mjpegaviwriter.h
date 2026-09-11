#pragma once
#include <QFile>
#include <QList>

// MJPEG AVI container writer. UI signals and stream selection belong to its caller.
class MjpegAviWriter {
public:
    bool open(const QString &path, int width, int height);
    void append(const QByteArray &jpeg, int width, int height);
    bool finish();
    bool hasFrames() const { return !m_recordingFrames.isEmpty(); }
private:
    QFile m_recordingFile;
    struct AviFrame { quint32 offset; quint32 size; };
    QList<AviFrame> m_recordingFrames;
    int m_recordingWidth{0};
    int m_recordingHeight{0};
    qint64 m_recordingRiffSizeOffset{0};
    qint64 m_recordingFrameCountOffset{0};
    qint64 m_recordingStreamLengthOffset{0};
    qint64 m_recordingMoviSizeOffset{0};
    qint64 m_recordingMoviDataOffset{0};
};
