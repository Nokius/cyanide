import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.cyanide 1.0

import "../js/Misc.js" as Misc

Dialog {
    id: page
    property string name: "friend"
    allowedOrientations: Orientation.All
    Component.onCompleted: {
        pages.push("Friend.qml")
        setProperties()
    }
    Component.onDestruction: {
        pages.pop()
        friendNumberStack.pop()
        if(inputField.focus)
            cyanide.send_typing_notification(f, false)
        refreshMessageList()
    }

    onOrientationChanged: {
        messageListView.positionViewAtEnd()
    }

    canAccept: true
    acceptDestination: Qt.resolvedUrl("FriendAction.qml")
    onAccepted: {
        pages.push("FriendAction.qml")
    }

    property int f: activeFriend()
    property int callstate: 0
    property string friend_name: ""
    property string friend_status_icon: ""
    property bool online: false

    function setProperties() {
        var entry = friendList.get(listFid(f))
        callstate = entry.friend_callstate
        friend_name = entry.friend_name
        friend_status_icon = entry.friend_status_icon
        online = entry.friend_connection_status
    }

    Connections {
        target: cyanide
        onSignal_friend_connection_status: setProperties()
        onSignal_friend_status: setProperties()
    }

    DockedPanel {
        id: fileControlPanel

        width: Theme.itemSizeLarge
        height: parent.height
        dock: Dock.Right

        open: false
        visible: open

        property int m: -1
        property int file_status: 99
        property bool incoming: false

        function togglePaused() {
            togglePaused.enabled = false
            console.log("togglePaused() was called")
            var errmsg
            if(file_status == File_State.Active || file_status == File_State.Paused_Them) {
                errmsg = cyanide.pause_transfer(f, m)
                if(errmsg === "") {
                    console.log("paused successfully, closing panel")
                } else {
                    cyanide.notify_error(qsTr("Failed to pause transfer"), qsTr(errmsg))
                }
            } else if(file_status == File_State.Paused_Us || file_status == File_State.Paused_Both) {
                errmsg = cyanide.resume_transfer(f, m)
                if(errmsg === "") {
                    console.log("paused successfully, closing panel")
                } else {
                    cyanide.notify_error(qsTr("Failed to resume transfer"), qsTr(errmsg))
                }
            } else if(file_status == File_State.Cancelled) {
                console.log("attempted to pause/resume a cancelled transfer")
            } else if(file_status == File_State.Finished) {
                console.log("attempted to pause/resume a finished transfer")
            }
            file_status = cyanide.get_file_status(f, m)
            open = false
            togglePaused.enabled = true
        }
        function cancel() {
            cancel.enabled = false
            console.log("cancel() was called")
            if(file_status == File_State.Finished || file_status == File_State.Cancelled) {
                console.log("attempted to cancel a finished/cancelled transfer")
            } else {
                var errmsg =  cyanide.cancel_transfer(f, m)
                if(errmsg === "")
                    open = false
                else
                    cyanide.notify_error(qsTr("Failed to cancel transfer"), qsTr(errmsg))
            }
            open = false
            cancel.enabled = true
        }

        function toggleIcons() {
            if(file_status == File_State.Paused_Us || file_status == File_State.Paused_Both) {
                togglePaused.icon.source = "image://theme/icon-cover-play"
                togglePaused.visible = true
                cancel.visible = true
            } else if(file_status == File_State.Active || file_status == File_State.Paused_Them) {
                togglePaused.icon.source = "image://theme/icon-cover-pause"
                togglePaused.visible = true
                cancel.visible = true
            } else if(file_status == File_State.Cancelled || file_status == File_State.Finished) {
                togglePaused.visible = false
                cancel.visible = false
            }
        }

        onFile_statusChanged: {
            toggleIcons()
        }

        Column {
            anchors {
                centerIn: parent
            }
            spacing: Theme.itemSizeLarge
            IconButton {
                id: togglePaused
                onClicked: fileControlPanel.togglePaused()
            }
            IconButton {
                id: cancel
                onClicked: fileControlPanel.cancel()
                icon.source: "image://theme/icon-cover-cancel"
            }
        }
    }

    SilicaFlickable {
        id: content
        anchors {
            fill: parent
            rightMargin: fileControlPanel.visibleSize
        }

        PullDownMenu {
            id: pullDownMenu
            visible: callstate == -2

            MenuItem {
                id: accept
                //: av call
                // text: qsTr("Answer")
                onClicked: {
                    cyanide.av_invite_accept(f)
                }
            }
        }

        PushUpMenu {
            id: pushUpMenu
            visible: callstate != 0

            MenuItem {
                id: reject
                //: av call
                // text: qsTr("Ignore")
                visible: callstate == -2
                onClicked: cyanide.av_invite_reject(f)
            }
            MenuItem {
                id: cancelCall
                //: outgoing call
                // text: qsTr("Cancel")
                visible: callstate == -1
                onClicked: cyanide.av_call_cancel(f)
            }
            MenuItem {
                id: hangup
                // text: qsTr("Hang up")
                visible: callstate > 0
                onClicked: cyanide.av_hangup(f)
            }
        }

        PageHeader {
            id: pageHeader
            Label {
                id: title
                text: friend_name
                font.pixelSize: Theme.fontSizeMedium
                color: Theme.highlightColor
                y: pageHeader.height / 2 - height / 2
                anchors {
                    right: parent.right
                    rightMargin: Theme.paddingLarge
                }
            }
            Image {
                id: friendStatusIcon
                source: friend_status_icon
                y: pageHeader.height / 2 - height / 2
                anchors {
                    right: title.left
                    rightMargin: Theme.paddingSmall + width/2
                }
            }
        }

        SilicaListView {
            id: messageListView

            model: messageList

            anchors {
                fill: parent
                topMargin: pageHeader.height
                bottomMargin: inputField.height - Theme.paddingSmall
            }

            Component.onCompleted: {
                refreshMessageList()
                messageListView.positionViewAtEnd()
                cyanide.set_friend_activity(f, false)
            }

            Connections {
                target: cyanide
                onSignal_friend_message: {
                    messageListView.positionViewAtEnd()
                }
                onSignal_friend_typing: {
                    if(fid == f) {
                        inputField.label = is_typing
                            ? friend_name + qsTr(" is typing...")
                            : ""
                        inputField.placeholderText = inputField.label
                    }
                }
                onSignal_file_status: {
                    if(fileControlPanel.open
                            && fid == f
                            && mid == fileControlPanel.m
                            && (status == File_State.Finished || status == File_State.Cancelled))
                    {
                        console.log("transfer finished, closing panel")
                        fileControlPanel.open = false
                    }
                }
            }

            delegate: Item {
                id: delegate
                height: Theme.paddingMedium + message.height + inlineImage.height + timestampLabel.height

                Image {
                    id: attach
                    visible: message.file
                    y: message.y
                    source: "image://theme/icon-s-attach"
                    x: message.x - width
                }

                MouseArea {
                    id: mouseArea
                    width: message.width
                    height: message.height
                    anchors {
                        fill: message
                    }
                    onClicked: {
                        enabled = false
                        message.visible = false
                        selectableMessage.visible = true
                        selectableMessage.focus = true
                    }
                }
                Image {
                    id: inlineImage
                    visible: m_type == Message_Type.Image && (m_author || f_progress == 100)
                    x: Theme.paddingLarge
                    y: message.y + message.height + Theme.paddingSmall
                    width: content.width - 2 * Theme.paddingLarge > implicitWidth ?
                               implicitWidth : content.width - 2 * Theme.paddingLarge
                    fillMode: Image.PreserveAspectFit
                    source: visible ? m_text : ""
                }
                TextArea {
                    id: selectableMessage
                    visible: false

                    y: message.y - Theme.paddingSmall
                    x: message.x - Theme.paddingLarge
                    height: implicitHeight
                    width: message.width + 2 * Theme.paddingLarge + Theme.paddingMedium
                    readOnly: true
                    focusOnClick: true
                    onFocusChanged: {
                        if(!focus) {
                            mouseArea.enabled = true
                            visible = false
                            focus = false
                            message.visible = true
                        }
                    }

                    text: m_text
                    horizontalAlignment: message.horizontalAlignment
                    font.pixelSize: message.font.pixelSize
                }
                Label {
                    id: message
                    visible: true
                    property bool file: m_type & Message_Type.File
                    text: file ? f_status == 0 ?
                              "<s>(" + f_progress + "%) " + m_text.replace(/.*\//, "") + "</s>"
                               : "(" + f_progress + "%) " + f_link
                               : m_rich_text
                    Component.onCompleted: {
                        var limit = content.width * 2/3
                        if(width > limit)
                            width = limit
                    }

                    x: m_author ? Theme.paddingSmall + attach.width
                                : content.width - Math.max(width, timestampLabel.width) - Theme.paddingLarge
                    font.pixelSize: Theme.fontSizeExtraSmall
                    // color: m_author ? Theme.secondaryColor : Theme.primaryColor
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.Wrap
                    textFormat: file && f_status == 0 ? Text.RichText : Text.StyledText

                    linkColor: Theme.highlightColor
                    onLinkActivated: {
                        if(!file) {
                            cyanide.notify_error(qsTr("Opening URL..."), "")
                            Misc.openUrl(link)
                        } else {
                            if(fileControlPanel.open) {
                                fileControlPanel.open = false
                            } else if((f_status == File_State.Active)
                                    ||(f_status &  File_State.Paused)) {
                                openFileControlPanel(m_id, f_status)
                            } else if(f_status == File_State.Cancelled) {
                                ;
                            } else if(f_status == File_State.Finished) {
                                cyanide.notify_error(qsTr("Opening file..."), m_text)
                                console.log(link)
                                Misc.openUrl(link)
                            }
                        }
                    }
                }
                function openFileControlPanel(m, f_status) {
                    fileControlPanel.m = parseInt(m)
                    fileControlPanel.incoming = !m_author
                    fileControlPanel.file_status = f_status
                    fileControlPanel.open = true
                    fileControlPanel.toggleIcons()
                }

                Label {
                    id: timestampLabel
                    text: Misc.elapsedTime(m_timestamp)
                    x: message.x
                    y: message.y + message.height + Theme.paddingSmall
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    horizontalAlignment: m_author ? Text.AlignRight : Text.AlignLeft
                }
            }
            VerticalScrollDecorator {}
        }

        IconButton {
            id: submit
            icon.source: "qrc:/images/sendmessage"
            y: inputField.y + inputField.height - height - Theme.paddingLarge
            x: content.width - width - Theme.paddingSmall
            onClicked: inputField.dispatch()
            visible: inputField.focus
        }

        TextArea {
            id: inputField
            font.pixelSize: Theme.fontSizeExtraSmall
            width: content.width - submit.width - Theme.paddingLarge - Theme.paddingSmall
            inputMethodHints: Qt.ImhNoAutoUppercase
            focus: false
            onFocusChanged: cyanide.send_typing_notification(f, focus)
            onYChanged: {
                if(focus)
                    messageListView.positionViewAtEnd()
            }
            anchors {
                bottom: parent.bottom
            }
            function dispatch() {
                if(text === "" || !online) {
                    return
                }
                var errmsg = cyanide.send_friend_message(f, text)
                if(errmsg === "") {
                    focus = false
                    parent.focus = true;
                    text = ""
                } else {
                    cyanide.notify_error(qsTr("Failed to send message"), errmsg)
                }
            }
        }
    }
}
