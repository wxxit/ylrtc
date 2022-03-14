var client = null;
client = new Ylrtc.Client("ffrtc.com", 8000);
var has_local_video = true;
var has_local_audio = true;
var publisheStream = null;
var userName = "";
var remoteStreamMap = new Map();

(function(w){
  var promptBoxExist = false;
  var promptBox = null;
  var textShow = null;
  var enter = null;

  function promptWindow(text, cb) {
    var textContext = text ? text : "";
    var callBack = cb ? cb : function() {};
    if (promptBoxExist == false){
      promptBox = document.createElement("div");
      promptBox.style.cssText=`position:fixed;top:0;left:0px;z-index:10000;width:100%;height:100%;background-color:rgba(0,0,0,0.5);transition:all 0.5s;`;
      promptBox.id="promptBox";

      var enterContent = document.createElement("div");
      enterContent.style.cssText = `position:absolute;top:50%;left:50%;width:350px;transform:translate(-50%,-50%);overflow:hidden;min-height:140px;background-color:rgba(247, 241, 244, 0.774);border-radius:10px;`;

      textShow = document.createElement("div");
      textShow.style.cssText = `padding:10px 10px 10px 20px;font-size:16px;min-height:60px;overflow:hidden;word-break:break-all;`;

      var btnGroup = document.createElement("div");
      btnGroup.style.cssText = `width:100%;height:40px;padding:10px;margin-bottom:10px;display:flex;justify-content:center;align-items:center;box-sizing: border-box;`;

      enter = document.createElement("div");
      enter.style.cssText = `color:white;padding:8px 16px;background-color:#0094c7;border-radius:20px;font-size:16px;`;
      enter.innerText = "Ok";

      btnGroup.appendChild(enter);
      enterContent.appendChild(textShow);
      enterContent.appendChild(btnGroup);
      promptBox.appendChild(enterContent);
      document.body.appendChild(promptBox);
    } else if (promptBoxExist){
      promptBox.style.display="block";
    }
    textShow.innerText= textContext;
    promptBoxExist = true;

    enter.onclick = function () {
      promptBox.style.display = "none";
      callBack && callBack();
    }
  }
  w.promptWindow = promptWindow;
})(window);

class RemoteStreamView {
  constructor(streamId, UserId, hasVideo, hasAudio, subscribeStream) {
    this.close = false;
    this.subscribeStream = subscribeStream;
    let self = this;
    this.subscribeStream.addEventListener("mute", (type) => {
      if (type == "video")
        self._changeRemoteStreamCameraImg(true);
      else if (type == "audio")
        self._changeRemoteStreamMicrophoneImg(true);
    });
    this.subscribeStream.addEventListener("unmute", (type) => {
      if (type == "video")
        self._changeRemoteStreamCameraImg(false);
      else if (type == "audio")
        self._changeRemoteStreamMicrophoneImg(false);
    });
    this.subscribeStream.addEventListener("ended", (subscribeStreamId) => {
      self.close = true;
      document.getElementById(subscribeStreamId).remove();
      remoteStreamMap.delete(subscribeStreamId);
    });
    this.streamId = streamId;
    this.hasVideo = hasVideo;
    this.hasAudio = hasAudio;
    this.videoMuted = !hasVideo;
    this.audioMuted = !hasAudio;
    let remoteStreamDiv = document.createElement('div');
    remoteStreamDiv.id = streamId;
    remoteStreamDiv.style = "display: flex; flex-direction: column; border-style: solid; border-color:rgba(60, 108, 219, 0.466);";
    let video = document.createElement('video');
    video.srcObject = this.subscribeStream.mediaStream();
    video.style = "height: 80%; background-color: rgb(7, 7, 7);";
    let playPromise = video.play();
    if (playPromise !== undefined) {
      playPromise.then(() => {
      }).catch(error => {
        if (!self.close) {
          window.promptWindow("Playback failed. Click resume playback.", () => {
            video.play();
          });
        }
      });
    }
    remoteStreamDiv.append(video);
    let right = document.getElementById("right");
    let remoteStreamControls = document.createElement('div');
    remoteStreamControls.style = "display: flex; flex-direction: row; background-color: rgb(64, 72, 83); \
    height: 32px; padding: 5px; justify-content: flex-end; flex-basis: 10%; flex-shrink: 0";
    let remoteStreamUserName = document.createElement('div');
    remoteStreamUserName.textContent = UserId;
    remoteStreamUserName.style = "vertical-align: middle; line-height: 32px; color: rgb(240, 212, 228); font-size : 28px; text-align: center; flex-basis: 60%; flex-grow: 1;";
    remoteStreamControls.append(remoteStreamUserName);
    this.remoteStreamCamera = document.createElement('img');
    if (this.hasVideo)
      this.remoteStreamCamera.src = "./img/camera-open.png";
    else
      this.remoteStreamCamera.src = "./img/camera-close.png";
    this.remoteStreamCamera.style = "margin-right: 5px; width: 32px; height: 32px;";
    this.remoteStreamCamera.onclick = this._onRemoteStreamCameraImgClicked.bind(this);
    remoteStreamControls.append(this.remoteStreamCamera);
    this.remoteStreamMicrophone = document.createElement('img');
    if (this.hasAudio)
      this.remoteStreamMicrophone.src = "./img/microphone-open.png";
    else
      this.remoteStreamMicrophone.src = "./img/microphone-close.png";
    this.remoteStreamMicrophone.style = "width: 32px; height: 32px;";
    this.remoteStreamMicrophone.onclick = this._onRemoteStreamMicrophoneImgClicked.bind(this);
    remoteStreamControls.append(this.remoteStreamMicrophone);
    remoteStreamDiv.append(remoteStreamControls);
    right.append(remoteStreamDiv);
  }

  _changeRemoteStreamCameraImg(muted) {
    if (muted)
      this.remoteStreamCamera.src = "./img/camera-close.png";
    else
      this.remoteStreamCamera.src = "./img/camera-open.png";
  }

  _changeRemoteStreamMicrophoneImg(muted) {
    if (muted)
      this.remoteStreamMicrophone.src = "./img/microphone-close.png";
    else
      this.remoteStreamMicrophone.src = "./img/microphone-open.png";
  }

  _onRemoteStreamCameraImgClicked() {
    if (!this.hasVideo)
      return;
    this.videoMuted = !this.videoMuted;
    this._changeRemoteStreamCameraImg(this.videoMuted);
    if (this.videoMuted)
      this.subscribeStream.mute("video");
    else
      this.subscribeStream.unmute("video");
  }

  _onRemoteStreamMicrophoneImgClicked() {
    if (!this.hasAudio)
      return;
    this.audioMuted = !this.audioMuted;
    this._changeRemoteStreamMicrophoneImg(this.audioMuted);
    if (this.audioMuted)
      this.subscribeStream.mute("audio");
    else
      this.subscribeStream.unmute("audio");
  }
};
function randomString(e) {
  e = e || 32;
  var t = "ABCDEFGHJKMNPQRSTWXYZabcdefhijkmnprstwxyz2345678",
  a = t.length,
  n = "";
  for (i = 0; i < e; i++)
    n += t.charAt(Math.floor(Math.random() * a));
  return n
}
var copyInvitationLinkBtn = document.getElementById("left-title-invitation-link");
copyInvitationLinkBtn.onclick = () => {
  if (navigator.clipboard) {
      navigator.clipboard.writeText(window.location.href);
  } else {
      var textarea = document.createElement('textarea');
      document.body.appendChild(textarea);
      textarea.style.position = 'fixed';
      textarea.style.clip = 'rect(0 0 0 0)';
      textarea.style.top = '10px';
      textarea.value = window.location.href;
      textarea.select();
      document.execCommand('copy', true);
      document.body.removeChild(textarea);
  }
}
window.onload = function() {
  userName = randomString(8);
  client.join("9527", userName).then(async (roomInfo) => {
    console.log("Join room success.");
    console.log("room info = ", roomInfo);
    client.addEventListener("streamAdded", async (e) => {
      console.log("add stream id = " + e.publishStreamId);
      if (e.participantId === userName)
        return;
      let subscribeStream = await client.subscribe(e);
      console.log("subscribe stream id ", subscribeStream.id());
      let subscribeStreamView = new RemoteStreamView(subscribeStream.id(), e.participantId, e.hasVideo, e.hasAudio, subscribeStream);
      remoteStreamMap.set(subscribeStream.id(), subscribeStreamView);
    });

    let have_video = false;
    let have_audio = false;
    try {
      let cameras = await Ylrtc.DeviceEnumerator.enumerateCameras();
      if (cameras.length > 0)
        have_video = true;
    } catch(e) {
      console.log("No camera.");
    }
    try {
      let microphones = await Ylrtc.DeviceEnumerator.enumerateMicrophones();
      if (microphones.length > 0)
        have_audio = true;
    } catch(e) {
      console.log("No microphone.");
    }

    if (!have_audio && !have_video)
      return;
    let device = await Ylrtc.Device.CreateDevice("camera", {audio: have_audio, video: have_video});
    let localPlayer = document.getElementById('local-video');
    localPlayer.srcObject = device.mediaStream();
    let local_video_header_title = document.getElementById("local-video-header-title");
    local_video_header_title.textContent = userName;
    let audioRtpEncodingParameters = [{maxBitrate: 64000}];
    let simulcast = true;
    let videoRtpEncodingParameters;
    if (!simulcast)
      videoRtpEncodingParameters = [{maxBitrate: 500000}];
    else
      videoRtpEncodingParameters = [{active: true}, {active: true}, {active: true}];
    publisheStream = await client.publish(device, audioRtpEncodingParameters, videoRtpEncodingParameters);
    let localStreamCameraImg = document.getElementById('local-video-camera');
    localStreamCameraImg.src = "./img/camera-open.png";
    localStreamCameraImg.onclick = () => {
      has_local_video = !has_local_video;
      if (has_local_video) {
        publisheStream.unmute("video");
        localStreamCameraImg.src = "./img/camera-open.png";
      }
      else {
        publisheStream.mute("video");
        localStreamCameraImg.src = "./img/camera-close.png";
      }
    }
    let localStreamMicrophone = document.getElementById('local-video-microphone');
    localStreamMicrophone.src = "./img/microphone-open.png";
    localStreamMicrophone.onclick = () => {
      has_local_audio = !has_local_audio;
      if (has_local_audio) {
        publisheStream.unmute("audio");
        localStreamMicrophone.src = "./img/microphone-open.png";
      }
      else {
        publisheStream.mute("audio");
        localStreamMicrophone.src = "./img/microphone-close.png";
      }
    }

    roomInfo.streams.forEach( async function(e) {
      if (e.participantId === userName)
        return;
      let subscribeStream = await client.subscribe(e);
      console.log("subscribe stream id ", subscribeStream.id());
      let subscribeStreamView = new RemoteStreamView(subscribeStream.id(), e.participantId, e.hasVideo, e.hasAudio, subscribeStream);
      remoteStreamMap.set(subscribeStream.id(), subscribeStreamView);
    });
  }).catch(() => {
    console.log("Join room failed.");
  });

}
window.onbeforeunload = function (event) {
  if (client)
    client.leave();
}