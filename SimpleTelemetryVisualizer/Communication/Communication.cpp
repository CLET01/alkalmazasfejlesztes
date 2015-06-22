#include <QTimer>
#include "Communication.h"
#include "Communication/Parcel.h"

Communication::Communication()
    : receiveStream(nullptr), currentMessageSize(0)
{
}

Communication::~Communication()
{
    if (receiveStream != nullptr)
    {
        delete receiveStream;
        receiveStream = nullptr;
    }
}

void Communication::connectToDevice(QIODevice *device)
{
    // Connect receive stream
    if (receiveStream != nullptr)
    {
        delete receiveStream;
    }
    receiveStream = new QDataStream(device);
}

std::unique_ptr<QDataStream> Communication::getSendStream()
{
    return std::unique_ptr<QDataStream>(new QDataStream(&sendBuffer, QIODevice::WriteOnly));
}

QDataStream *Communication::getReceiveStream()
{
    return receiveStream;
}

void Communication::send(const Parcel& parcel)
{
    // TODO: check connection status
    auto stream = getSendStream();

    // Store start position in stream to calculate message size
    const qint64 startPos = stream->device()->size();
    qint32 msgSize = 0;
    // Temporally, write 0 message size.
    // We will come back here and set the correct value
    //  after serialization is complete.
    *stream << msgSize;
    // Send ID, timestamp and value
    parcel.WriteTo(*stream);
    const qint64 endPos = stream->device()->size();

    // Jump back to the beginning and write the correct message size.
    stream->device()->seek(startPos);
    msgSize = endPos - startPos;
    *stream << msgSize;
    // Jump to the end of the serialized data stream.
    stream->device()->seek(endPos);

    // Send the data (w.r.t. the used protocol)
    sendBufferContent();
}

void Communication::dataReceived()
{
    // Read as long as a whole message is received. After that, emit Communication::dataReady signal.
    QDataStream &inStream = *getReceiveStream();
    QIODevice *socket = inStream.device();

    // It's a new block
    if (currentMessageSize == 0) {
        /* Még nem tudjuk a csomag méretét... */
        // There's not enough bytes arrived to determine the size
        if (socket->bytesAvailable() < (int) sizeof(qint32)) {
            /* Még a csomag mérete sem jött meg. */
            return;
        }

        // Computing blockSize
        inStream >> currentMessageSize;
    }
    /* Már tudjuk a csomag méretét. */

    if (socket->bytesAvailable() < (int) (currentMessageSize - sizeof(qint32))) {
        /* Nem jött még meg az egész csomag. */
        return;
    }

    /* Jelezzük, hogy van új adat. Amit átadunk, az az id és méret utáni adattartalom.
     * Tömb esetében a QVector úgy szerializálja ki magát, hogy abban benne van a méret is. */
    emit dataReady(inStream);

    // Maybe we got the first bytes of a next packet
    currentMessageSize = 0;
    if (socket->bytesAvailable() > 0) {
        /* A QTimer-t használva még egyszer belelövünk
         * ebbe a slotba, hogy feldolgozza a maradék fogadott bytokat is. */
        QTimer::singleShot(0, this, SLOT(dataReceived()));
    }
}