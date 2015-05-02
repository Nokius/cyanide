#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <thread>
#include <time.h>

#include <sailfishapp.h>
#include <mlite5/mnotification.h>
#include <mlite5/mremoteaction.h>
#include <QObject>
#include <QtDBus>
#include <QtQuick>
#include <QTranslator>

#include "cyanide.h"
#include "dbusinterface.h"
#include "tox_bootstrap.h"
#include "tox_callbacks.h"
#include "util.h"
#include "dns.cpp"
#include "settings.h"

/* oh boy, here we go... */
#define UINT32_MAX (4294967295U)

struct Tox_Options tox_options;

int main(int argc, char *argv[])
{
    QGuiApplication *app = SailfishApp::application(argc, argv);
    app->setOrganizationName("Tox");
    app->setOrganizationDomain("Tox");
    app->setApplicationName("Cyanide");

    Cyanide *cyanide = new Cyanide();
    DBusInterface *relay = new DBusInterface();
    relay->cyanide = cyanide;

    if(!(QDBusConnection::sessionBus().registerObject("/", relay, QDBusConnection::ExportScriptableContents)
                                         && QDBusConnection::sessionBus().registerService("harbour.cyanide")))
        QGuiApplication::exit(0);

    cyanide->wifi_monitor();

    cyanide->read_default_profile(app->arguments());

    std::thread my_tox_thread(start_tox_thread, cyanide);

    qmlRegisterType<Message_Type>("harbour.cyanide", 1, 0, "Message_Type");
    qmlRegisterType<File_State>("harbour.cyanide", 1, 0, "File_State");
    cyanide->view->rootContext()->setContextProperty("cyanide", cyanide);
    cyanide->view->rootContext()->setContextProperty("settings", &cyanide->settings);
    cyanide->view->setSource(SailfishApp::pathTo("qml/cyanide.qml"));
    cyanide->view->showFullScreen();

    QObject::connect(cyanide->view, SIGNAL(visibilityChanged(QWindow::Visibility)),
                     cyanide, SLOT(visibility_changed(QWindow::Visibility)));

    int result = app->exec();

    cyanide->loop = LOOP_FINISH;
    //settings->close_databases();
    my_tox_thread.join();

    return result;
}

Cyanide::Cyanide(QObject *parent) : QObject(parent)
{
    view = SailfishApp::createView();

    events = eventfd(0, 0);
    check_wifi();
}

QString Cyanide::tox_save_file()
{
    return tox_save_file(profile_name);
}

QString Cyanide::tox_save_file(QString name)
{
    return TOX_DATA_DIR + name.replace('/', '_') + ".tox";
}

/* sets profile_name */
void Cyanide::read_default_profile(QStringList args)
{
    for(int i = 0; i < args.size(); i++) {
        if(args[i].startsWith("tox:")) {
            //TODO
        }
    }

    QFile file(DEFAULT_PROFILE_FILE);
    if(file.exists()) {
        file.open(QIODevice::ReadOnly | QIODevice::Text);
        profile_name = file.readLine();
        file.close();
        profile_name.chop(1);
        if(QFile::exists(tox_save_file()))
            return;
    }

    profile_name = DEFAULT_PROFILE_NAME;
    write_default_profile();
}

void Cyanide::write_default_profile()
{
    QFile file(DEFAULT_PROFILE_FILE);
    file.open(QIODevice::WriteOnly | QIODevice::Text);
    file.write(profile_name.toUtf8() + '\n');
    file.close();
}

void Cyanide::reload()
{
    loop = LOOP_RELOAD;
}

void Cyanide::load_new_profile()
{
    QString path;
    int i = 0;
    do {
        i++;
        path = TOX_DATA_DIR + "id" + QString::number(i) + ".tox";
    } while(QFile::exists(path));
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.close();
    load_tox_save_file(path);
}

void Cyanide::delete_current_profile()
{
    bool success;
    loop = LOOP_STOP;
    usleep(1500 * MAX_ITERATION_TIME);
    success = QFile::remove(tox_save_file());
    qDebug() << "removed" << tox_save_file() << ":" << success;
    success = QFile::remove(CYANIDE_DATA_DIR + profile_name.replace('/','_') + ".sqlite");
    qDebug() << "removed db:" << success;
}

void Cyanide::load_tox_save_file(QString path)
{
    QString basename = path.mid(path.lastIndexOf('/') + 1);
    if(!basename.endsWith(".tox"))
        basename += ".tox";

    /* ensure that the save file is in ~/.config/tox */
    QFile::copy(path, TOX_DATA_DIR + basename);

    /* remove the .tox extension */
    basename.chop(4);
    next_profile_name = basename;

    if(loop == LOOP_STOP) {
        // tox_loop not running, resume it
        profile_name = basename;
        resume_thread();
    } else {
        loop = LOOP_RELOAD_OTHER;
    }
}

void Cyanide::check_wifi()
{
    if(settings.get("wifi-only") == "true") {
        if(0 == system("test true = \"$(dbus-send --system --dest=net.connman --print-reply"
                                      " /net/connman/technology/wifi net.connman.Technology.GetProperties"
                                      "| grep -A1 Connected | sed -e 1d -e 's/^.*\\s//')\"")) {
            qDebug() << "connected via wifi, toxing";
            if(loop == LOOP_SUSPEND)
                resume_thread();
            else
                loop = LOOP_RUN;
        } else {
            qDebug() << "not connected via wifi, not toxing";
            suspend_thread();
        }
    } else {
        if(loop == LOOP_SUSPEND)
            resume_thread();
        else
            loop = LOOP_RUN;
    }
}

void Cyanide::wifi_monitor()
{
    QDBusConnection dbus = QDBusConnection::systemBus();

    if(!dbus.isConnected()) {
        qDebug() << "Failed to connect to the D-Bus session bus.";
    }
    dbus.connect("net.connman",
                 "/net/connman/technology/wifi",
                 "net.connman.Technology",
                 "PropertyChanged",
                 this,
                 SLOT(wifi_changed(QString, QDBusVariant)));
}

void Cyanide::wifi_changed(QString name, QDBusVariant dbus_variant)
{
    bool value = dbus_variant.variant().toBool();
    qDebug() << "Received DBus signal";
    qDebug() << "Property" << name << "Value" << value;

    if(name == "Powered") {
        ;
    } else if(name == "Connected" && settings.get("wifi-only") == "true") {
        if(value && loop == LOOP_SUSPEND) {
            qDebug() << "connected, resuming thread";
            resume_thread();
        } else if(!value && loop == LOOP_RUN) {
            qDebug() << "not connected, suspending thread";
            suspend_thread();
        }
    }
}

void Cyanide::resume_thread()
{
    loop = LOOP_RUN;
    uint64_t event = 1;
    ssize_t tmp = write(events, &event, sizeof(event));
    Q_ASSERT(tmp == sizeof(event));
}

void Cyanide::suspend_thread()
{
    loop = LOOP_SUSPEND;
    self.connection_status = TOX_CONNECTION_NONE;
    emit signal_friend_connection_status(SELF_FRIEND_NUMBER, false);
    for(auto it = friends.begin(); it != friends.end(); it++) {
        it->second.connection_status = TOX_CONNECTION_NONE;
        emit signal_friend_connection_status(it->first, false);
    }
    usleep(1500 * MAX_ITERATION_TIME);
}

void Cyanide::visibility_changed(QWindow::Visibility visibility)
{
    /* remove all notifications for now until I find a proper solution
     * (because the error messages are shown too)
     */
    for(std::pair<uint32_t, Friend>pair : friends) {
        Friend f = pair.second;
        if(f.notification != NULL) {
            f.notification = NULL;
        }
    }
    for(MNotification *n : MNotification::notifications()) {
        n->remove();
    }
}

void Cyanide::on_message_notification_activated(int fid)
{
    qDebug() << "notification activated";
    emit signal_focus_friend(fid);
    raise();
}

void Cyanide::notify_error(QString summary, QString body)
{
    MNotification *n = new MNotification("", summary, body);
    n->publish();
}

void Cyanide::notify_message(int fid, QString summary, QString body)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    Friend *f = &friends[fid];

    if(f->notification != NULL) {
        f->notification->remove();
    }
    f->notification = new MNotification("harbour.cyanide.message", summary, body);
    MRemoteAction action("harbour.cyanide", "/", "harbour.cyanide", "message_notification_activated",
            QVariantList() << fid);
    f->notification->setAction(action);
    f->notification->publish();
}

void Cyanide::notify_call(int fid, QString summary, QString body)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    Friend *f = &friends[fid];

    if(f->notification != NULL) {
        f->notification->remove();
    }
    f->notification = new MNotification("harbour.cyanide.call", summary, body);
    MRemoteAction action("harbour.cyanide", "/", "harbour.cyanide", "message_notification_activated",
            QVariantList() << fid);
    f->notification->setAction(action);
    f->notification->publish();
}

void Cyanide::raise()
{
    if(!is_visible()) {
        view->raise();
        view->requestActivate();
    }
}

bool Cyanide::is_visible()
{
    return view->visibility() == QWindow::FullScreen;
}

void Cyanide::load_tox_and_stuff_pretty_please()
{
    int error;

    settings.open_database(profile_name);

    self.user_status = TOX_USER_STATUS_NONE;
    self.connection_status = TOX_CONNECTION_NONE;
    memset(&self.avatar_transfer, 0, sizeof(File_Transfer));

    //TODO use this destructor thingy
    for(std::pair<uint32_t, Friend>pair : friends) {
        Friend f = pair.second;
        free(f.avatar_transfer.filename);
        f.files.clear();
        for(Message m : f.messages) {
            if(m.ft != NULL) {
                free(m.ft->filename);
                free(m.ft);
            }
        }
        f.messages.clear();
    }
    friends.clear();

    save_needed = false;

    // TODO free
    tox_options = *tox_options_new((TOX_ERR_OPTIONS_NEW*)&error);
    // TODO switch(error)

    size_t save_data_size;
    const uint8_t *save_data = get_save_data(&save_data_size);
    if(settings.get("udp-enabled") != "true")
        tox_options.udp_enabled = 0;
    tox = tox_new(&tox_options, save_data, save_data_size, (TOX_ERR_NEW*)&error);
    // TODO switch(error)

    tox_self_get_address(tox, self_address);
    tox_self_get_public_key(tox, self.public_key);

    QString public_key = get_friend_public_key(SELF_FRIEND_NUMBER);
    settings.add_friend_if_not_exists(public_key);

    QByteArray saved_hash = settings.get_friend_avatar_hash(public_key);
    memcpy(self.avatar_hash, saved_hash.constData(), TOX_HASH_LENGTH);

    if(save_data_size == 0 || save_data == NULL)
        load_defaults();
    else
        load_tox_data();

    emit signal_friend_added(SELF_FRIEND_NUMBER);
    emit signal_avatar_change(SELF_FRIEND_NUMBER);

    qDebug() << "Name:" << self.name;
    qDebug() << "Status" << self.status_message;
    qDebug() << "Tox ID" << get_self_address();
}

void start_tox_thread(Cyanide *cyanide)
{
    cyanide->tox_thread();
}

void start_toxav_loop(Cyanide *cyanide)
{
    cyanide->toxav_loop();
}

void Cyanide::tox_thread()
{
    qDebug() << "profile name" << profile_name;
    qDebug() << "tox_save_file" << tox_save_file();

    load_tox_and_stuff_pretty_please();

    set_callbacks();

    // Connect to bootstraped nodes in "tox_bootstrap.h"

    do_bootstrap();

    // Start the tox av session.
    TOXAV_ERR_NEW error;
    toxav = toxav_new(tox, &error);
    switch(error) {
        case TOXAV_ERR_NEW_OK:
            break;
        case TOXAV_ERR_NEW_NULL:
            Q_ASSERT(false);
            break;
        case TOXAV_ERR_NEW_MALLOC:
            qWarning() << "Failed to allocate memory for toxav";
            break;
        case TOXAV_ERR_NEW_MULTIPLE:
            qWarning() << "Attemted to create second toxav session";
            break;
    };

    // Give toxcore the av functions to call
    set_av_callbacks();

    check_wifi();

    tox_loop();
}

void Cyanide::tox_loop()
{
    uint64_t now, last_save = get_time();
    TOX_CONNECTION c, connection = c = TOX_CONNECTION_NONE;

    std::thread toxav_thread(start_toxav_loop, this);

    while(loop == LOOP_RUN) {

        tox_iterate(tox);

        // Check current connection
        if((c = tox_self_get_connection_status(tox)) != connection) {
            self.connection_status = connection = c;
            emit signal_friend_connection_status(SELF_FRIEND_NUMBER, c != TOX_CONNECTION_NONE);
            qDebug() << (c != TOX_CONNECTION_NONE ? "Connected to DHT" : "Disconnected from DHT");
        }

        now = get_time();

        // Wait 1 million ticks then reconnect if needed and write save
        if(now - last_save >= (uint64_t)10 * 1000 * 1000 * 1000) {
            last_save = now;

            if(connection == TOX_CONNECTION_NONE) {
                do_bootstrap();
            }

            if (save_needed || (now - last_save >= (uint)100 * 1000 * 1000 * 1000)) {
                write_save();
            }
        }

        usleep(1000 * MIN(tox_iteration_interval(tox), MAX_ITERATION_TIME));
    }

    toxav_thread.join();

    uint64_t event;
    ssize_t tmp;
    switch(loop) {
        case LOOP_RUN:
            Q_ASSERT(false);
            break;
        case LOOP_FINISH:
            qDebug() << "exiting...";
            killall_tox();
            break;
        case LOOP_RELOAD:
            killall_tox();
            tox_thread();
            break;
        case LOOP_RELOAD_OTHER:
            qDebug() << "loading profile" << next_profile_name;
            killall_tox();
            profile_name = next_profile_name;
            write_default_profile();
            tox_thread();
            break;
        case LOOP_SUSPEND:
            tmp = read(events, &event, sizeof(event));
            Q_ASSERT(tmp == sizeof(event));
            qDebug() << "read" << event << ", resuming thread";
            tox_loop();
            break;
        case LOOP_STOP:
            killall_tox();
            tmp = read(events, &event, sizeof(event));
            Q_ASSERT(tmp = sizeof(event));
            qDebug() << "read" << event << ", starting thread with profile" << profile_name
                        << "save file" << tox_save_file();
            write_default_profile();
            tox_thread();
            break;
    }
}

void Cyanide::toxav_loop()
{
    while(loop == LOOP_RUN) {
        toxav_iterate(toxav);
        usleep(1000 * toxav_iteration_interval(toxav));
    }
}

void Cyanide::killall_tox()
{
    toxav_kill(toxav);
    kill_tox();
}

void Cyanide::kill_tox()
{
    TOX_ERR_FRIEND_ADD error;

    /* re-add all blocked friends */
    for(auto it = friends.begin(); it != friends.end(); it++) {
        Friend *f = &it->second;

        if(f->blocked) {
            tox_friend_add_norequest(tox, f->public_key, &error);

            if(error == TOX_ERR_FRIEND_ADD_MALLOC) {
                qDebug() << "memory allocation failure";
            } else {
                f->blocked = false;
            }
        }
    }

    write_save();

    tox_kill(tox);
}

/* bootstrap to dht with bootstrap_nodes */
void Cyanide::do_bootstrap()
{
    TOX_ERR_BOOTSTRAP error;
    static unsigned int j = 0;

    if (j == 0)
        j = rand();

    int i = 0;
    while(i < 4) {
        struct bootstrap_node *d = &bootstrap_nodes[j % countof(bootstrap_nodes)];
        tox_bootstrap(tox, d->address, d->port, d->key, &error);
        i++;
        j++;
    }
}

void Cyanide::load_defaults()
{
    TOX_ERR_SET_INFO error;

    uint8_t *name = (uint8_t*)DEFAULT_NAME.data(), *status = (uint8_t*)DEFAULT_STATUS.data();
    uint16_t name_len = DEFAULT_NAME.toUtf8().size() , status_len = DEFAULT_STATUS.toUtf8().size();

    self.name = DEFAULT_NAME;
    self.status_message = DEFAULT_STATUS;

    tox_self_set_name(tox, name, name_len, &error);
    tox_self_set_status_message(tox, status, status_len, &error);

    emit signal_friend_name(SELF_FRIEND_NUMBER, NULL);
    emit signal_friend_status_message(SELF_FRIEND_NUMBER);
    save_needed = true;
}

void Cyanide::write_save()
{
    void *data;
    uint32_t size;

    size = tox_get_savedata_size(tox);
    data = malloc(size);
    tox_get_savedata(tox, (uint8_t*)data);

    QDir().mkpath(TOX_DATA_DIR);
    QDir().mkpath(TOX_AVATAR_DIR);

    QSaveFile file(tox_save_file());
    if(!file.open(QIODevice::WriteOnly)) {
        qDebug() << "failed to open save file";
    }

    file.write((const char*)data, size);

    file.commit();

    save_needed = false;
    free(data);
}

const uint8_t* Cyanide::get_save_data(size_t *size)
{
    void *data;

    data = file_raw(tox_save_file().toUtf8().data(), size);
    if(!data)
        *size = 0;

    return (const uint8_t*)data;
}

void Cyanide::load_tox_data()
{
    TOX_ERR_FRIEND_QUERY error;
    size_t length;
    size_t nfriends = tox_self_get_friend_list_size(tox);
    qDebug() << "Loading" << nfriends << "friends...";

    for(size_t i = 0; i < nfriends; i++) {
        Friend f = *new Friend();

        tox_friend_get_public_key(tox, i, f.public_key, (TOX_ERR_FRIEND_GET_PUBLIC_KEY*)&error);

        uint8_t hex_id[2 * TOX_PUBLIC_KEY_SIZE];
        public_key_to_string((char*)hex_id, (char*)f.public_key);
        QString public_key = utf8_to_qstr(hex_id, 2 * TOX_PUBLIC_KEY_SIZE);
        settings.add_friend_if_not_exists(public_key);
        QByteArray saved_hash = settings.get_friend_avatar_hash(public_key);
        memcpy(f.avatar_hash, saved_hash.constData(), TOX_HASH_LENGTH);

        length = tox_friend_get_name_size(tox, i, &error);
        uint8_t name[length];
        if(!tox_friend_get_name(tox, i, name, &error)) {
            if(error == TOX_ERR_FRIEND_QUERY_FRIEND_NOT_FOUND)
                    qDebug() << "Friend not found" << i;
        }
        f.name = utf8_to_qstr(name, length);

        length = tox_friend_get_status_message_size(tox, i, &error);
        uint8_t status_message[length];
        if(!tox_friend_get_status_message(tox, i, status_message, &error)) {
            if(error == TOX_ERR_FRIEND_QUERY_FRIEND_NOT_FOUND)
                    qDebug() << "Friend not found" << i;
        }
        f.status_message = utf8_to_qstr(status_message, length);

        add_friend(&f);
    }

    length = tox_self_get_name_size(tox);
    uint8_t name[length];
    tox_self_get_name(tox, name);
    self.name = utf8_to_qstr(name, length);

    length = tox_self_get_status_message_size(tox);
    uint8_t status_message[length];
    tox_self_get_status_message(tox, status_message);
    self.status_message = utf8_to_qstr(status_message, length);

    emit signal_friend_name(SELF_FRIEND_NUMBER, NULL);
    emit signal_friend_status_message(SELF_FRIEND_NUMBER);
    save_needed = true;
}

uint32_t Cyanide::add_friend(Friend *f)
{
    uint32_t fid = next_friend_number();
    friends[fid] = *f;
    emit signal_friend_added(fid);
    return fid;
}

/* find out the friend number that toxcore will assign when using
 * tox_friend_add() and tox_friend_add_norequest() */
uint32_t Cyanide::next_friend_number()
{
    uint32_t fid;
    for(fid = 0; fid < UINT32_MAX; fid++) {
        if(friends.count(fid) == 0 || friends[fid].blocked)
            break;
    }
    return fid;
}

uint32_t Cyanide::next_but_one_unoccupied_friend_number()
{
    int count = 0;
    uint32_t fid;
    for(fid = 0; fid < UINT32_MAX; fid++) {
        if(friends.count(fid) == 0 && count++)
            break;
    }
    return fid;
}

void Cyanide::relocate_blocked_friend()
{
    uint32_t from = next_friend_number();

    if(friends.count(from) == 1 && friends[from].blocked) {
        uint32_t to = next_but_one_unoccupied_friend_number();
        friends[to] = friends[from];
        friends.erase(from);
        emit signal_friend_added(to);
    }
}

void Cyanide::add_message(uint32_t fid, Message message)
{
    friends[fid].messages.append(message);
    uint32_t mid = friends[fid].messages.size() - 1;
    qDebug() << "added message number" << mid;

    if(message.ft != NULL)
        friends[fid].files[message.ft->file_number] = mid;

    if(!message.author)
        set_friend_activity(fid, true);

    emit signal_friend_message(fid, mid, message.type);
}

void Cyanide::set_callbacks()
{
    tox_callback_friend_request(tox, callback_friend_request, this);
    tox_callback_friend_message(tox, callback_friend_message, this);
    tox_callback_friend_name(tox, callback_friend_name, this);
    tox_callback_friend_status_message(tox, callback_friend_status_message, this);
    tox_callback_friend_status(tox, callback_friend_status, this);
    tox_callback_friend_typing(tox, callback_friend_typing, this);
    tox_callback_friend_read_receipt(tox, callback_friend_read_receipt, this);
    tox_callback_friend_connection_status(tox, callback_friend_connection_status, this);

    tox_callback_file_recv(tox, callback_file_recv, this);
    tox_callback_file_recv_chunk(tox, callback_file_recv_chunk, this);
    tox_callback_file_recv_control(tox, callback_file_recv_control, this);
    tox_callback_file_chunk_request(tox, callback_file_chunk_request, this);
}

void Cyanide::set_av_callbacks()
{
    /*
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_invite, av_OnInvite, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_start, av_OnStart, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_cancel, av_OnCancel, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_reject, av_OnReject, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_end, av_OnEnd, this);

    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_ringing, av_OnRinging, this);

    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_requesttimeout, av_OnRequestTimeout, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_peertimeout, av_OnPeerTimeout, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_selfmediachange, av_OnSelfCSChange, this);
    toxav_register_callstate_callback(toxav, (ToxAVCallback)callback_av_peermediachange, av_OnPeerCSChange, this);

    toxav_register_audio_callback(toxav, (ToxAvAudioCallback)callback_av_audio, this);
    toxav_register_video_callback(toxav, (ToxAvVideoCallback)callback_av_video, this);
    */
}

void Cyanide::send_typing_notification(int fid, bool typing)
{
    TOX_ERR_SET_TYPING error;
    if(settings.get("send-typing-notifications") == "true")
        tox_self_set_typing(tox, fid, typing, &error);
}

QString Cyanide::send_friend_request(QString id_str, QString msg_str)
{
    /* honor the Tox URI scheme */
    if(id_str.startsWith("tox:"))
        id_str = id_str.remove(0,4);

    size_t id_len = qstrlen(id_str);
    char id[id_len];
    qstr_to_utf8((uint8_t*)id, id_str);

    if(msg_str.isEmpty())
        msg_str = DEFAULT_FRIEND_REQUEST_MESSAGE;

    size_t msg_len = qstrlen(msg_str);
    uint8_t msg[msg_len];
    qstr_to_utf8(msg, msg_str);

    uint8_t address[TOX_ADDRESS_SIZE];
    if(!string_to_address((char*)address, (char*)id)) {
        /* not a regular id, try DNS discovery */
        if(!dns_request(address, id_str))
            return tr("Error: Invalid Tox ID");
    }

    QString errmsg = send_friend_request_id(address, msg, msg_len);
    if(errmsg == "") {
        Friend *f = new Friend((const uint8_t*)address, id_str, "");
        add_friend(f);
        char hex_address[2 * TOX_ADDRESS_SIZE + 1];
        address_to_string(hex_address, (char*)address);
        hex_address[2 * TOX_ADDRESS_SIZE] = '\0';
        settings.add_friend(hex_address);
    }

    return errmsg;
}

/* */
QString Cyanide::send_friend_request_id(const uint8_t *id, const uint8_t *msg, size_t msg_length)
{
    TOX_ERR_FRIEND_ADD error;
    relocate_blocked_friend();
    uint32_t UNUSED(fid) = tox_friend_add(tox, id, msg, msg_length, &error);
    switch(error) {
        case TOX_ERR_FRIEND_ADD_OK:
            return "";
        case TOX_ERR_FRIEND_ADD_NULL:
            return "Error: Null";
        case TOX_ERR_FRIEND_ADD_TOO_LONG:
            return tr("Error: Message is too long");
        case TOX_ERR_FRIEND_ADD_NO_MESSAGE:
            return "Bug (please report): Empty message";
        case TOX_ERR_FRIEND_ADD_OWN_KEY:
            return tr("Error: Tox ID is self ID");
        case TOX_ERR_FRIEND_ADD_ALREADY_SENT:
            return tr("Error: Tox ID is already in friend list");
        case TOX_ERR_FRIEND_ADD_BAD_CHECKSUM:
            return tr("Error: Invalid Tox ID (bad checksum)");
        case TOX_ERR_FRIEND_ADD_SET_NEW_NOSPAM:
            return tr("Error: Invalid Tox ID (bad nospam value)");
        case TOX_ERR_FRIEND_ADD_MALLOC:
            return tr("Error: No memory");
    }
    return "";
}

std::vector<QString> split_message(QString rest)
{
    std::vector<QString> messages;
    QString chunk;
    int last_space;

    while(rest != "") {
        chunk = rest.left(TOX_MAX_MESSAGE_LENGTH);
        last_space = chunk.lastIndexOf(" ");

        if(chunk.size() == TOX_MAX_MESSAGE_LENGTH) {
            if(last_space > 0) {
                chunk = rest.left(last_space);
            }
        }
        rest = rest.right(rest.size() - chunk.size());

        messages.push_back(chunk);
    }
    return messages;
}

QString Cyanide::send_friend_message(int fid, QString message)
{
    TOX_ERR_FRIEND_SEND_MESSAGE error;
    QString errmsg = "";

    if(friends[fid].blocked)
        /* TODO show this somehow in the UI */
        return "Friend is blocked, unblock to connect";

    TOX_MESSAGE_TYPE type = TOX_MESSAGE_TYPE_NORMAL;

    std::vector<QString> messages = split_message(message);

    for(size_t i = 0; i < messages.size(); i++) {
        QString msg_str = messages[i];

        size_t msg_len = qstrlen(msg_str);
        uint8_t msg[msg_len];
        qstr_to_utf8(msg, msg_str);

        uint32_t message_id = tox_friend_send_message(tox, fid, type, msg, msg_len, &error);
        qDebug() << "message id:" << message_id;

        switch(error) {
            case TOX_ERR_FRIEND_SEND_MESSAGE_OK:
                break;
            case TOX_ERR_FRIEND_SEND_MESSAGE_NULL:
                Q_ASSERT(false);
                break;
            case TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_FOUND:
                Q_ASSERT(false);
                break;
            case TOX_ERR_FRIEND_SEND_MESSAGE_FRIEND_NOT_CONNECTED:
                errmsg = tr("Error: Friend not connected");
                break;
            case TOX_ERR_FRIEND_SEND_MESSAGE_SENDQ:
                Q_ASSERT(false);
                break;
            case TOX_ERR_FRIEND_SEND_MESSAGE_TOO_LONG:
                qDebug() << "message too long";
                break;
            case TOX_ERR_FRIEND_SEND_MESSAGE_EMPTY:
                Q_ASSERT(false);
                break;
        }

        if(errmsg != "")
            return errmsg;

        Message m;
        m.type = Message_Type::Normal;
        m.author = true;
        m.text = msg_str;
        m.timestamp = QDateTime::currentDateTime();
        m.ft = NULL;

        add_message(fid, m);
    }

    return errmsg;
}

bool Cyanide::accept_friend_request(int fid)
{
    TOX_ERR_FRIEND_ADD error;
    relocate_blocked_friend();
    if(tox_friend_add_norequest(tox, friends[fid].public_key, &error) == UINT32_MAX) {
        qDebug() << "could not add friend";
        return false;
    }

    friends[fid].accepted = true;
    save_needed = true;
    return true;
}

void Cyanide::remove_friend(int fid)
{
    TOX_ERR_FRIEND_DELETE error;
    if(friends[fid].accepted && !tox_friend_delete(tox, fid, &error)) {
        qDebug() << "Failed to remove friend";
        return;
    }
    save_needed = true;
    settings.remove_friend(get_friend_public_key(fid));
    friends.erase(fid);
}

/* setters and getters */

QString Cyanide::get_profile_name()
{
    return profile_name;
}

QString Cyanide::set_profile_name(QString name)
{
    QString old_name = tox_save_file();
    QString new_name = tox_save_file(name);

    QFile old_file(old_name);
    QFile new_file(new_name);

    if(new_file.exists()) {
        return tr("Error: File exists");
    } else {
        save_needed = false;
        /* possible race condition? */
        old_file.rename(new_name);
        QFile::rename(CYANIDE_DATA_DIR + profile_name.replace('/','_') + ".sqlite",
                    CYANIDE_DATA_DIR + name.replace('/','_') + ".sqlite");
        profile_name = name;
        save_needed = true;
        return "";
    }
}

QList<int> Cyanide::get_friend_numbers()
{
    QList<int> friend_numbers;
    for(auto it = friends.begin(); it != friends.end(); it++) {
        friend_numbers.append(it->first);
    }
    return friend_numbers;
}

QList<int> Cyanide::get_message_numbers(int fid)
{
    QList<int> message_numbers;
    auto messages = friends[fid].messages;
    int i = 0;
    for(auto it = messages.begin(); it != messages.end(); it++) {
        message_numbers.append(i++);
    }
    return message_numbers;
}

void Cyanide::set_friend_activity(int fid, bool status)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    friends[fid].activity = status;
    emit signal_friend_activity(fid);
}

bool Cyanide::get_friend_blocked(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    return f.blocked;
}

void Cyanide::set_friend_blocked(int fid, bool block)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    int error;

    Friend *f = &friends[fid];

    if(block) {
        if(tox_friend_delete(tox, fid, (TOX_ERR_FRIEND_DELETE*)&error)) {
            f->connection_status = TOX_CONNECTION_NONE;
        } else {
            qDebug() << "Failed to remove friend";
        }
    } else {
        int friend_number = tox_friend_add_norequest(tox, f->public_key, (TOX_ERR_FRIEND_ADD*)&error);
        Q_ASSERT(friend_number == fid);
    }
    friends[fid].blocked = block;
    emit signal_friend_blocked(fid, block);
    emit signal_friend_connection_status(fid, f->connection_status != TOX_CONNECTION_NONE);
}

int Cyanide::get_friend_callstate(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    return f.callstate;
}

void Cyanide::set_self_name(QString name)
{
    bool success;
    TOX_ERR_SET_INFO error;
    self.name = name;

    size_t length = qstrlen(name);
    uint8_t tmp[length];
    qstr_to_utf8(tmp, name);
    success = tox_self_set_name(tox, tmp, length, &error);
    Q_ASSERT(success);

    save_needed = true;
    emit signal_friend_name(SELF_FRIEND_NUMBER, NULL);
}

void Cyanide::set_self_status_message(QString status_message)
{
    bool success;
    TOX_ERR_SET_INFO error;
    self.status_message = status_message;

    size_t length = qstrlen(status_message);
    uint8_t tmp[length];
    qstr_to_utf8(tmp, status_message);

    success = tox_self_set_status_message(tox, tmp, length, &error);
    Q_ASSERT(success);

    save_needed = true;
    emit signal_friend_status_message(SELF_FRIEND_NUMBER);
}

int Cyanide::get_self_user_status()
{
    switch(self.user_status) {
        case TOX_USER_STATUS_NONE:
            return 0;
        case TOX_USER_STATUS_AWAY:
            return 1;
        case TOX_USER_STATUS_BUSY:
            return 2;
    }
    return 0;
}

void Cyanide::set_self_user_status(int status)
{
    switch(status) {
        case 0:
            self.user_status = TOX_USER_STATUS_NONE;
            break;
        case 1:
            self.user_status = TOX_USER_STATUS_AWAY;
            break;
        case 2:
            self.user_status = TOX_USER_STATUS_BUSY;
            break;
    }
    tox_self_set_status(tox, self.user_status);
    emit signal_friend_status(SELF_FRIEND_NUMBER);
}

QString Cyanide::set_self_avatar(QString new_avatar)
{
    bool success;
    QString public_key = get_friend_public_key(SELF_FRIEND_NUMBER);
    QString old_avatar = TOX_AVATAR_DIR + public_key + QString(".png");

    if(new_avatar == "") {
        /* remove the avatar */
        success = QFile::remove(old_avatar);
        if(!success) {
            qDebug() << "Failed to remove avatar file";
        } else {
            memset(self.avatar_hash, 0, TOX_HASH_LENGTH);
            emit signal_avatar_change(SELF_FRIEND_NUMBER);
            send_new_avatar();
        }
        return "";
    }

    uint32_t size;
    uint8_t *data = (uint8_t*)file_raw(new_avatar.toUtf8().data(), &size);
    if(data == NULL) {
        return tr("File not found: ") + new_avatar;
    }

    if(size > MAX_AVATAR_SIZE) {
        free(data);
        return tr("Avatar too large. Maximum size: 64KiB");
    }
    uint8_t previous_hash[TOX_HASH_LENGTH];
    memcpy(previous_hash, self.avatar_hash, TOX_HASH_LENGTH);
    success = tox_hash(self.avatar_hash, data, size);
    Q_ASSERT(success);
    FILE* out = fopen(old_avatar.toUtf8().data(), "wb");
    fwrite(data, size, 1, out);
    fclose(out);
    free(data);
    if(0 != memcmp(previous_hash, self.avatar_hash, TOX_HASH_LENGTH)) {
        QByteArray hash((const char*)self.avatar_hash, TOX_HASH_LENGTH);
        settings.set_friend_avatar_hash(public_key, hash);
    } else {
        /* friend avatar data is the same as before, so we don't
         * need to save it.
         * send it anyway, other clients will probably reject it
         */
    }
    emit signal_avatar_change(SELF_FRIEND_NUMBER);
    send_new_avatar();

    return "";
}

void Cyanide::send_new_avatar()
{
    /* send the avatar to each connected friend */
    for(auto i=friends.begin(); i!=friends.end(); i++) {
        Friend *f = &i->second;
        if(f->connection_status != TOX_CONNECTION_NONE) {
            QString errmsg = send_avatar(i->first);
            if(errmsg != "")
                qDebug() << "Failed to send avatar. " << errmsg;
        }
    }
}

QString Cyanide::get_friend_name(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    return f.name;
}

QString Cyanide::get_friend_avatar(int fid)
{
    QString avatar = TOX_AVATAR_DIR + get_friend_public_key(fid) + QString(".png");
    return QFile::exists(avatar) ? avatar : "qrc:/images/blankavatar";
}

QString Cyanide::get_friend_status_message(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    return f.status_message;
}

QString Cyanide::get_friend_status_icon(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    return get_friend_status_icon(fid, f.connection_status != TOX_CONNECTION_NONE);
}

QString Cyanide::get_friend_status_icon(int fid, bool online)
{
    QString url = "qrc:/images/";
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];

    if(!online) {
        url.append("offline");
    } else {
        switch(f.user_status) {
        case TOX_USER_STATUS_NONE:
            url.append("online");
            break;
        case TOX_USER_STATUS_AWAY:
            url.append("away");
            break;
        case TOX_USER_STATUS_BUSY:
            url.append("busy");
            break;
        }
    }
    
    if(f.activity)
        url.append("_notification");

    url.append("_2x");

    return url;
}

bool Cyanide::get_friend_connection_status(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    return f.connection_status != TOX_CONNECTION_NONE;
}

bool Cyanide::get_friend_accepted(int fid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    return friends[fid].accepted;
}

QString Cyanide::get_friend_public_key(int fid)
{
    Friend f = fid == SELF_FRIEND_NUMBER ? self : friends[fid];
    uint8_t hex_id[2 * TOX_PUBLIC_KEY_SIZE];
    public_key_to_string((char*)hex_id, (char*)f.public_key);
    return utf8_to_qstr(hex_id, 2 * TOX_PUBLIC_KEY_SIZE);
}

QString Cyanide::get_self_address()
{
    char hex_address[2 * TOX_ADDRESS_SIZE];
    address_to_string((char*)hex_address, (char*)self_address);
    return utf8_to_qstr(hex_address, 2 * TOX_ADDRESS_SIZE);
}

int Cyanide::get_message_type(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    return friends[fid].messages[mid].type;
}

QString Cyanide::get_message_text(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    return friends[fid].messages[mid].text;
}

QString Cyanide::get_message_html_escaped_text(int fid, int mid)
{
    return get_message_text(fid, mid).toHtmlEscaped();
}

QString Cyanide::get_message_rich_text(int fid, int mid)
{
    QString text = get_message_html_escaped_text(fid, mid);

    const QString email_chars = QRegExp::escape("A-Za-z0-9!#$%&'*+-/=?^_`{|}~.");
    const QString url_chars =   QRegExp::escape("A-Za-z0-9!#$%&'*+-/=?^_`{|}~");
    const QString email_token = "[" + email_chars + "]+";
    const QString url_token = "[" + url_chars + "]+";

    /* match either protocol:address or email@domain or example.org */
    QRegExp rx("(\\b[A-Za-z][A-Za-z0-9]*:[^\\s]+\\b"
               "|\\b" + email_token + "@" + email_token + "\\b"
               "|\\b" + url_token  + "\\." + email_token + "\\b)");
    QString repl;
    int protocol;
    int pos = 0;

     while((pos = rx.indexIn(text, pos)) != -1) {
         /* check whether the captured text has a protocol identifier */
         protocol = rx.cap(1).indexOf(QRegExp("^[A-Za-z0-9]+:"), 0);
         repl = "<a href=\"" +
                 QString(  (protocol != -1) ? ""
                         : (rx.cap(1).indexOf("@") != -1) ? "mailto:"
                         : "https:")
                 + rx.cap(1) + "\">" + rx.cap(1) + "</a>";
         text.replace(pos, rx.matchedLength(), repl);
         pos += repl.length();
     }

     QRegExp ry("(^"+QRegExp::escape("&gt;")+"[^\n]*\n"
                "|^"+QRegExp::escape("&gt;")+"[^\n]*$)"
                );
     pos = 0;
     while((pos = ry.indexIn(text, pos)) != -1) {
         repl = "<font color='lightgreen'>"+ry.cap(1)+"</font>";
         text.replace(pos, ry.matchedLength(), repl);
         pos += repl.length();
     }
     text.replace("\n", "<br>");

    return text;
}

QDateTime Cyanide::get_message_timestamp(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    return friends[fid].messages[mid].timestamp;
}

bool Cyanide::get_message_author(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    return friends[fid].messages[mid].author;
}

int Cyanide::get_file_status(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    return friends[fid].messages[mid].ft->status;
}

QString Cyanide::get_file_link(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    QString filename = friends[fid].messages[mid].text;
    QString fullpath = "file://" + filename;
    return "<a href=\"" + fullpath.toHtmlEscaped() + "\">"
            + filename.right(filename.size() - 1 - filename.lastIndexOf("/")).toHtmlEscaped()
            + "</a>";
}

int Cyanide::get_file_progress(int fid, int mid)
{
    Q_ASSERT(fid != SELF_FRIEND_NUMBER);
    File_Transfer *ft = friends[fid].messages[mid].ft;

    return ft->file_size == 0 ? 100
                              : 100 * ft->position / ft->file_size;
}
