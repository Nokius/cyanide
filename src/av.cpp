#include <AL/al.h>
#include <AL/alc.h>

#include "cyanide.h"

#include "unused.h"
#include "util.h"

uint32_t Cyanide::get_audio_bit_rate()
{
    return 123;
}

uint32_t Cyanide::video_bit_rate()
{
    return 231;
}

void callback_call(ToxAV *av, uint32_t fid, uint32_bool audio_enabled, bool video_enabled, void *that)
{
    qDebug() << "was called";
    Cyanide *cyanide = (Cyanide*)that;

    Friend *f = &cyanide->friends[fid];
//    f->call_index = call_index;
//    f->callstate = -2;
//    emit cyanide->signal_friend_callstate(fid, f->callstate);
//    emit cyanide->signal_av_invite(fid);
}

void callback_call_state(ToxAV *av, uint32_t fid, uint32_t state, void *that)
{
    qDebug() << "was called";
}

void callback_audio_bit_rate_status(ToxAV *av, uint32_t fid, bool stable, uint32_t bit_rate, void *that)
{
    qDebug() << "was called";
}

void callback_video_bit_rate_status(ToxAV *av, uint32_t fid, bool stable, uint32_t bit_rate, void *that)
{
    qDebug() << "was called";
}

void callback_receive_video_frame(ToxAV *av, uint32_t fid,
                                  uint16_t width, uint16_t height,
                                  uint8_t const *y, uint8_t const *u, uint8_t const *v,
                                  int32_t ystride, int32_t ustride, int32_t vstride,
                                  void *that)
{
    qDebug() << "was called";
}

void callback_receive_audio_frame(ToxAV *av, uint32_t friend_number,
                                  int16_t const *pcm,
                                  size_t sample_count,
                                  uint8_t channels,
                                  uint32_t sampling_rate,
                                  void *that)
{
    qDebug() << "was called";
}

bool Cyanide::call(int fid, bool audio, bool video)
{
    TOXAV_ERR_CALL error;
    uint32_t audio_bit_rate = audio ? audio_bit_rate() : 0;
    uint32_t video_bit_rate = video ? video_bit_rate() : 0;
    bool success = toxav_call(toxav, fid, audio_bit_rate, video_bit_rate, &error);
    switch(error) {
        case TOXAV_ERR_CALL_OK:
            break;
        case TOXAV_ERR_CALL_MALLOC:
            qWarning() << "malloc";
            break;
        case TOXAV_ERR_CALL_FRIEND_NOT_FOUND:
            qWarning() << "friend not found";
            break;
        case TOXAV_ERR_CALL_FRIEND_NOT_CONNECTED:
            qWarning() << "friend not connected";
            break;
        case TOXAV_ERR_CALL_FRIEND_ALREADY_IN_CALL:
            qWarning() << "friend already in call";
            break;
        case TOXAV_ERR_CALL_INVALID_BIT_RATE:
            qWarning() << "invalid bit rate";
            break;
    };
    return success;
}

bool Cyanide::answer(int fid)
{
    TOXAV_ERR_ANSWER error;
    bool success = toxav_answer(toxav, fid, audio_bit_rate(), video_bit_rate(), &error);
    switch(error) {
        case TOXAV_ERR_ANSWER_OK:
            break;
        case TOXAV_ERR_ANSWER_MALLOC:
            break;
        case TOXAV_ERR_ANSWER_FRIEND_NOT_FOUND:
            break;
        case TOXAV_ERR_ANSWER_FRIEND_NOT_CALLING:
            break;
        case TOXAV_ERR_ANSWER_INVALID_BIT_RATE:
            break;
    };
    return success;
}

bool Cyanide::send_call_control(int fid, TOX_CALL_CONTROL action)
{
    TOX_ERR_CALL_CONTROL error;
    bool success = toxav_call_control(toxav, fid, action, &error);
    switch(error) {
        case TOXAV_ERR_CALL_CONTROL_OK:
            break;
        case TOXAV_ERR_CALL_CONTROL_FRIEND_NOT_FOUND:
            qWarning() << "friend not found";
            break;
        case TOXAV_ERR_CALL_CONTROL_FRIEND_NOT_IN_CALL:
            qWarning() << "friend not in call";
            break;
        case TOXAV_ERR_CALL_CONTROL_NOT_PAUSED:
            qWarning() << "not paused";
            break;
        case TOXAV_ERR_CALL_CONTROL_DENIED:
            qWarning() << "denied";
            break;
        case TOXAV_ERR_CALL_CONTROL_ALREADY_PAUSED:
            qWarning() << "already paused";
            break;
        case TOXAV_ERR_CALL_CONTROL_NOT_MUTED:
            qWarning() << "not muted";
            break;
    };
    return success;
}

send_frame(int fid, bool video)
{
    bool success;
    TOXAV_ERR_SEND_FRAME error;

    if(video) {
        success = toxav_send_video_frame(toxav, fid,
                                         0, 0,             // x, y
                                         NULL, NULL, NULL, // Y, U, V
                                         &error);
    } else {
        pcm = NULL;
        channels = 2;
        sample_count = 0;
        success = toxav_send_audio_frame(toxav, fid,
                                         pcm,
                                         sample_count,
                                         channels,
                                         sampling_rate,
                                         &error);
    }

    switch(error) {
        case TOXAV_ERR_SEND_FRAME_OK:
            break;
        case TOXAV_ERR_SEND_FRAME_NULL:
            qWarning() << "null";
            break;
        case TOXAV_ERR_SEND_FRAME_FRIEND_NOT_FOUND:
            qWarning() << "friend not found";
            break;
        case TOXAV_ERR_SEND_FRAME_FRIEND_NOT_IN_CALL:
            qWarning() << "friend not in call";
            break;
        case TOXAV_ERR_SEND_FRAME_NOT_REQUESTED:
            qWarning() << "frame not requested";
            break;
        case TOXAV_ERR_SEND_FRAME_INVALID:
            qWarning() << "frame invalid";
            break;
        case TOXAV_ERR_SEND_FRAME_RTP_FAILED:
            qWarning() << "RTP failed";
            break;
    };
}

// set bit rates

/*
void Cyanide::av_invite_accept(int fid)
{
    Friend *f = &friends[fid];
//    ToxAvCSettings csettings = av_DefaultSettings;
//    toxav_answer(toxav, f->call_index, &csettings);
//    emit signal_friend_callstate(fid, (f->callstate = 2));
}

void Cyanide::av_invite_reject(int fid)
{
    Friend *f = &friends[fid];
//    toxav_reject(toxav, f->call_index, ""); //TODO add reason
    emit signal_friend_callstate(fid, (f->callstate = 0));
}

void Cyanide::av_call(int fid)
{
    Friend *f = &friends[fid];
    if(f->callstate != 0)
        notify_error("already in a call", "");

//    ToxAvCSettings csettings = av_DefaultSettings;

//    toxav_call(toxav, &f->call_index, fid, &csettings, 15);
    emit signal_friend_callstate(fid, (f->callstate = -1));
}

void Cyanide::av_call_cancel(int fid)
{
    qDebug() << "cancelling call";
    Friend *f = &friends[fid];
    Q_ASSERT(f->callstate == -1);
//    toxav_cancel(toxav, f->call_index, fid, "Call cancelled by friend");
    emit signal_friend_callstate(fid, (f->callstate = 0));
}

void Cyanide::av_hangup(int fid)
{
    qDebug() << "hanging up";
    Friend *f = &friends[fid];
    toxav_hangup(toxav, f->call_index);
    emit signal_friend_callstate(fid, (f->callstate = 0));
}
*/
