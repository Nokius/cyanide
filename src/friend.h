#ifndef FRIEND_H
#define FRIEND_H

#include <tox/tox.h>
#include <mlite5/mnotification.h>

#include "message.h"

class Call_State : public QObject
{
    Q_OBJECT
    Q_ENUMS(_)
public:
    enum _ { None      = 0
           , Incoming  = 0x0001
           , Outgoing  = 0x0002
           , Active    = 0x0004
           , Paused    = 0x0008
           };
};

class Friend
{
public:
    Friend();
    Friend(const uint8_t *public_key, QString name, QString status_message);

    QString name;
    QString status_message;

    uint8_t public_key[TOX_PUBLIC_KEY_SIZE];
    uint8_t avatar_hash[TOX_HASH_LENGTH];

    TOX_CONNECTION connection_status;
    TOX_USER_STATUS user_status;
    bool accepted;
    bool activity;
    bool blocked;

    int call_state;

    File_Transfer avatar_transfer;

    /* maps file_number to mid (message id)
     * mid == -1 is outgoing avatar
     * mid == -2 is incoming avatar
     */
    std::map<uint32_t, int> files;

    QList<Message> messages;

    MNotification *notification;

signals:

public slots:

};

#endif // FRIEND_H
