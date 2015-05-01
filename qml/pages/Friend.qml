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
    }
    Component.onDestruction: {
        pages.pop()
        friendNumberStack.pop()
        if(inputField.focus)
            cyanide.send_typing_notification(f, false)
    }

    onOrientationChanged: refreshMessageList()

    canAccept: true
    acceptDestination: Qt.resolvedUrl("FriendAction.qml")
    onAccepted: {
        pages.push("FriendAction.qml")
    }

    property int f: activeFriend()
    property int callstate: friendList.get(f+1).friend_callstate

    DockedPanel {
        id: fileControlPanel

        width: parent.width
        height: Theme.itemSizeExtraLarge

        dock: Dock.Top

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

        Row {
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
        anchors {
            fill: parent
            topMargin: page.isPortrait ? fileControlPanel.visibleSize : 0
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
                Component.onCompleted: text = friendList.get(f+1).friend_name
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
                Component.onCompleted: source = friendList.get(f+1).friend_status_icon
                y: pageHeader.height / 2 - height / 2
                anchors {
                    right: title.left
                    rightMargin: Theme.paddingMedium
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
                            ? friendList.get(f+1).friend_name + qsTr(" is typing...")
                            : ""
                        inputField.placeholderText = inputField.label
                    }
                }
                onSignal_file_status: {
                    if(fileControlPanel.open
                            && fid == f
                            && mid == fileControlPanel.m
                            && (status == 2 || status == 0))
                    {
                        console.log("transfer finished, closing panel")
                        fileControlPanel.open = false
                    }
                }
            }

            delegate: Item {
                id: delegate
                height: Theme.paddingMedium + message.height + inlineImage.height

                Image {
                    id: attach
                    visible: message.file
                    y: message.y
                    source: "image://theme/icon-s-attach"
                    x: message.x - width
                }

                MouseArea {
                    width: message.width
                    height: message.height
                    anchors {
                        fill: message
                    }
                    onClicked: {
                        if(inputField.copied || inputField.text === "") {
                            messageListView.positionViewAtEnd()
                            inputField.text = (m_text[0] === ">" ? ">" : "> ") + m_text + "\n"
                            inputField.cursorPosition = inputField.text.length
                        }
                    }
                }
                Image {
                    id: inlineImage
                    visible: m_type == Message_Type.Image && (m_author || f_progress == 100)
                    x: Theme.paddingLarge
                    y: message.y + message.height + Theme.paddingSmall
                    width: page.width - 2 * Theme.paddingLarge > implicitWidth ?
                               implicitWidth : page.width - 2 * Theme.paddingLarge
                    fillMode: Image.PreserveAspectFit
                    source: visible ? m_text : ""
                }
                Label {
                    id: message
                    property bool file: m_type & Message_Type.File
                    text: file ? f_status == 0 ?
                              "<s>(" + f_progress + "%) " + m_text.replace(/.*\//, "") + "</s>"
                               : "(" + f_progress + "%) " + f_link
                               : m_rich_text
                    Component.onCompleted: {
                        var limit = page.width * 2/3
                        if(width > limit)
                            width = limit
                    }

                    x: m_author ? Theme.paddingSmall + attach.width
                                : page.width - width - Theme.paddingLarge
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
                            } else if(f_status == 1 /* active  */
                                    ||f_status < 0) /* paused  */ {
                                openFileControlPanel(m_id, f_status)
                            } else if(f_status == 0) /* cancelled */ {
                                ;
                            } else if(f_status == 2) /* finished */ {
                                cyanide.notify_error(qsTr("Opening file..."), m_text)
                                console.log(link)
                                Misc.openUrl(link)
                            }
                        }
                    }
                }
                function openFileControlPanel(m, f_status) {
                    fileControlPanel.m = m
                    fileControlPanel.incoming = !m_author
                    fileControlPanel.file_status = f_status
                    fileControlPanel.open = true
                    fileControlPanel.toggleIcons()
                }

                /*
                Label {
                    id: timestampLabel
                    text: qsTr(Misc.elapsedTime(timestamp))
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    width: page.width/3 - 3*Theme.paddingMedium
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    horizontalAlignment: m_author ? Text.AlignRight : Text.AlignLeft
                    anchors {
                        right: m_author ? parent.right : messageLabel.left
                        left: m_author ? messageLabel.right : parent.left
                    }
                }
                */
            }
            VerticalScrollDecorator {}
        }

        IconButton {
            id: clear
            icon.source: "image://theme/icon-m-clear"
            y: inputField.y
            x: page.width - width - Theme.paddingSmall
            onClicked: inputField.text = ""
        }

        TextArea {
            id: inputField
            font.pixelSize: Theme.fontSizeExtraSmall
            width: parent.width - clear.width - Theme.paddingLarge - Theme.paddingSmall
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
            property bool copied: false
            onTextChanged: {
                if(focus)
                    copied = false
                else
                    copied = true
            }
            EnterKey.onClicked: {
                var online = friendList.get(f+1).friend_connection_status
                if(text === "" || !online) {
                    text = text.replace(/\n$/, "")
                    inputField.cursorPosition = inputField.text.length
                    return
                }
                // remove trailing newlines
                text = text.replace(/\n+$/, "")
                var errmsg = cyanide.send_friend_message(f, text)
                if(errmsg === "") {
                    text = ""
                    parent.focus = true;
                } else {
                    cyanide.notify_error(qsTr("Failed to send message"), errmsg)
                }
            }
        }
    }
    TextEdit {
        id: clipboard
        visible: false
        function setClipboard(value) {
        text = value
            selectAll()
            copy()
        }
    }
}
