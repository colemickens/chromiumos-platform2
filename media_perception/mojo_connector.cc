// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_perception/mojo_connector.h"

#include <utility>
#include <vector>

namespace mri {

namespace {

DeviceAccessResultCode GetDeviceAccessResultCode(
    video_capture::mojom::DeviceAccessResultCode code) {
  switch (code) {
    case video_capture::mojom::DeviceAccessResultCode::NOT_INITIALIZED:
      return DeviceAccessResultCode::NOT_INITIALIZED;
    case video_capture::mojom::DeviceAccessResultCode::SUCCESS:
      return DeviceAccessResultCode::SUCCESS;
    case video_capture::mojom::DeviceAccessResultCode::ERROR_DEVICE_NOT_FOUND:
      return DeviceAccessResultCode::ERROR_DEVICE_NOT_FOUND;
  }
  return DeviceAccessResultCode::RESULT_UNKNOWN;
}

PixelFormat GetPixelFormatFromVideoCapturePixelFormat(
    media::mojom::VideoCapturePixelFormat format) {
  switch (format) {
    case media::mojom::VideoCapturePixelFormat::I420:
      return PixelFormat::I420;
    case media::mojom::VideoCapturePixelFormat::MJPEG:
      return PixelFormat::MJPEG;
    default:
      return PixelFormat::FORMAT_UNKNOWN;
  }
  return PixelFormat::FORMAT_UNKNOWN;
}

media::mojom::VideoCapturePixelFormat GetVideoCapturePixelFormatFromPixelFormat(
    PixelFormat pixel_format) {
  switch (pixel_format) {
    case PixelFormat::I420:
      return media::mojom::VideoCapturePixelFormat::I420;
    case PixelFormat::MJPEG:
      return media::mojom::VideoCapturePixelFormat::MJPEG;
    default:
      return media::mojom::VideoCapturePixelFormat::UNKNOWN;
  }
  return media::mojom::VideoCapturePixelFormat::UNKNOWN;
}

constexpr char kConnectorPipe[] = "mpp-connector-pipe";

}  // namespace

MojoConnector::MojoConnector(): ipc_thread_("IpcThread") {
  mojo::edk::Init();
  LOG(INFO) << "Starting IPC thread.";
  if (!ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessageLoop::TYPE_IO, 0))) {
    LOG(ERROR) << "Failed to start IPC Thread";
  }
  mojo::edk::InitIPCSupport(ipc_thread_.task_runner());
}

void MojoConnector::ReceiveMojoInvitationFileDescriptor(int fd_int) {
  base::ScopedFD fd(fd_int);
  if (!fd.is_valid()) {
    LOG(ERROR) << "FD is not valid.";
    return;
  }
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::AcceptConnectionOnIpcThread,
                            base::Unretained(this), base::Passed(&fd)));
}

void MojoConnector::OnConnectionErrorOrClosed() {
  LOG(ERROR) << "Connection error/closed received";
}

void MojoConnector::OnDeviceFactoryConnectionErrorOrClosed() {
  std::lock_guard<std::mutex> lock(vcs_connection_state_mutex_);
  is_connected_to_vcs_ = false;
}

void MojoConnector::AcceptConnectionOnIpcThread(base::ScopedFD fd) {
  CHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  mojo::edk::SetParentPipeHandle(
      mojo::edk::ScopedPlatformHandle(mojo::edk::PlatformHandle(fd.release())));
  mojo::ScopedMessagePipeHandle child_pipe =
      mojo::edk::CreateChildMessagePipe(kConnectorPipe);
  if (!child_pipe.is_valid()) {
    LOG(ERROR) << "child_pipe is not valid";
  }
  media_perception_service_impl_ = std::make_unique<MediaPerceptionServiceImpl>(
      std::move(child_pipe),
      base::Bind(&MojoConnector::OnConnectionErrorOrClosed,
                 base::Unretained(this)));
}

void MojoConnector::ConnectToVideoCaptureService() {
  std::lock_guard<std::mutex> lock(vcs_connection_state_mutex_);
  if (!is_connected_to_vcs_) {
    ipc_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&MojoConnector::ConnectToVideoCaptureServiceOnIpcThread,
                   base::Unretained(this)));
    is_connected_to_vcs_ = true;
  }
}

void MojoConnector::ConnectToVideoCaptureServiceOnIpcThread() {
  media_perception_service_impl_->ConnectToVideoCaptureService(
      mojo::MakeRequest(&device_factory_));
  device_factory_.set_connection_error_handler(
      base::Bind(&MojoConnector::OnDeviceFactoryConnectionErrorOrClosed,
                 base::Unretained(this)));
}

bool MojoConnector::IsConnectedToVideoCaptureService() {
  std::lock_guard<std::mutex> lock(vcs_connection_state_mutex_);
  return is_connected_to_vcs_;
}

void MojoConnector::GetDevices(
    const VideoCaptureServiceClient::GetDevicesCallback& callback) {
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::GetDevicesOnIpcThread,
                            base::Unretained(this), callback));
}

void MojoConnector::GetDevicesOnIpcThread(
    const VideoCaptureServiceClient::GetDevicesCallback& callback) {
  device_factory_->GetDeviceInfos(base::Bind(
      &MojoConnector::OnDeviceInfosReceived, base::Unretained(this), callback));
}

void MojoConnector::OnDeviceInfosReceived(
    const VideoCaptureServiceClient::GetDevicesCallback& callback,
    std::vector<media::mojom::VideoCaptureDeviceInfoPtr> infos) {
  LOG(INFO) << "Got callback for device infos.";
  std::vector<SerializedVideoDevice> devices;
  for (const auto& capture_device : infos) {
    VideoDevice device;
    device.set_id(capture_device->descriptor->device_id);
    device.set_display_name(capture_device->descriptor->display_name);
    device.set_model_id(capture_device->descriptor->model_id);
    LOG(INFO) << "Device: " << device.display_name();
    for (const auto& capture_format : capture_device->supported_formats) {
      VideoStreamParams supported_format;
      supported_format.set_width_in_pixels(capture_format->frame_size->width);
      supported_format.set_height_in_pixels(capture_format->frame_size->height);
      supported_format.set_frame_rate_in_frames_per_second(
          capture_format->frame_rate);
      supported_format.set_pixel_format(
          GetPixelFormatFromVideoCapturePixelFormat(
          capture_format->pixel_format));
      *device.add_supported_configurations() = supported_format;
    }
    const int size = device.ByteSize();
    std::vector<uint8_t> bytes(size, 0);
    CHECK(device.SerializeToArray(bytes.data(), size))
        << "Failed to serialize mri::VideoDevice proto.";
    devices.push_back(bytes);
  }
  callback(devices);
}

void MojoConnector::SetActiveDevice(
    std::string device_id,
    const VideoCaptureServiceClient::SetActiveDeviceCallback& callback) {
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::SetActiveDeviceOnIpcThread,
                            base::Unretained(this), device_id, callback));
}

void MojoConnector::SetActiveDeviceOnIpcThread(
    std::string device_id,
    const VideoCaptureServiceClient::SetActiveDeviceCallback& callback) {
  device_factory_->CreateDevice(
      device_id, mojo::MakeRequest(&active_device_),
      base::Bind(&MojoConnector::OnSetActiveDeviceCallback,
                 base::Unretained(this), callback));
}

void MojoConnector::OnSetActiveDeviceCallback(
    const VideoCaptureServiceClient::SetActiveDeviceCallback& callback,
    video_capture::mojom::DeviceAccessResultCode code) {
  callback(GetDeviceAccessResultCode(code));
}

void MojoConnector::StartVideoCapture(
    const VideoStreamParams& capture_format,
    std::function<void(uint64_t timestamp_in_microseconds, const uint8_t* data,
                       int data_size)>
        frame_handler) {
  LOG(INFO) << "Setting frame handler.";
  receiver_impl_.SetFrameHandler(std::move(frame_handler));
  // Mojo code to start video capture and pass frames to the frame handler.
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::StartVideoCaptureOnIpcThread,
                            base::Unretained(this), capture_format));
}

void MojoConnector::StartVideoCaptureOnIpcThread(
    const VideoStreamParams& capture_format) {
  LOG(INFO) << "Starting video capture on ipc thread.";

  auto requested_settings = media::mojom::VideoCaptureParams::New();
  requested_settings->requested_format =
      media::mojom::VideoCaptureFormat::New();

  requested_settings->requested_format->frame_rate =
      capture_format.frame_rate_in_frames_per_second();

  requested_settings->requested_format->pixel_format =
      GetVideoCapturePixelFormatFromPixelFormat(capture_format.pixel_format());

  requested_settings->requested_format->frame_size = gfx::mojom::Size::New();
  requested_settings->requested_format->frame_size->width =
      capture_format.width_in_pixels();
  requested_settings->requested_format->frame_size->height =
      capture_format.height_in_pixels();

  requested_settings->buffer_type =
      media::mojom::VideoCaptureBufferType::kSharedMemoryViaRawFileDescriptor;
  active_device_->Start(std::move(requested_settings),
                        receiver_impl_.CreateInterfacePtr());
}

void MojoConnector::StopVideoCapture() {
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::StopVideoCaptureOnIpcThread,
                            base::Unretained(this)));
}

void MojoConnector::StopVideoCaptureOnIpcThread() {
  active_device_ = video_capture::mojom::DevicePtr();
}

void MojoConnector::CreateVirtualDevice(
    const VideoDevice& video_device,
    std::shared_ptr<ProducerImpl> producer_impl,
    const VideoCaptureServiceClient::VirtualDeviceCallback& callback) {
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::CreateVirtualDeviceOnIpcThread,
                            base::Unretained(this), video_device, producer_impl,
                            callback));
}

void MojoConnector::CreateVirtualDeviceOnIpcThread(
    const VideoDevice& video_device,
    std::shared_ptr<ProducerImpl> producer_impl,
    const VideoCaptureServiceClient::VirtualDeviceCallback& callback) {
  media::mojom::VideoCaptureDeviceInfoPtr info =
      media::mojom::VideoCaptureDeviceInfo::New();
  // TODO(b/3743548): After libchrome uprev, assigning to std::vector<> is
  // just redundant, so should be removed.
  info->supported_formats = std::vector<media::mojom::VideoCaptureFormatPtr>();
  info->descriptor = media::mojom::VideoCaptureDeviceDescriptor::New();
  info->descriptor->model_id = video_device.model_id();
  info->descriptor->device_id = video_device.id();
  info->descriptor->display_name = video_device.display_name();
  info->descriptor->capture_api = media::mojom::VideoCaptureApi::VIRTUAL_DEVICE;
  producer_impl->RegisterVirtualDeviceAtFactory(&device_factory_,
                                                std::move(info));

  const int size = video_device.ByteSize();
  std::vector<uint8_t> bytes(size /*count*/, 0 /*value*/);
  CHECK(video_device.SerializeToArray(bytes.data(), size))
      << "Failed to serialize mri::VideoDevice proto.";
  callback(bytes);
}

void MojoConnector::PushFrameToVirtualDevice(
    std::shared_ptr<ProducerImpl> producer_impl, base::TimeDelta timestamp,
    std::unique_ptr<const uint8_t[]> data, int data_size,
    PixelFormat pixel_format, int frame_width, int frame_height) {
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&MojoConnector::PushFrameToVirtualDeviceOnIpcThread,
                            base::Unretained(this), producer_impl, timestamp,
                            base::Passed(&data), data_size, pixel_format,
                            frame_width, frame_height));
}

void MojoConnector::PushFrameToVirtualDeviceOnIpcThread(
    std::shared_ptr<ProducerImpl> producer_impl, base::TimeDelta timestamp,
    std::unique_ptr<const uint8_t[]> data, int data_size,
    PixelFormat pixel_format, int frame_width, int frame_height) {
  producer_impl->PushNextFrame(
      producer_impl, timestamp, std::move(data), data_size,
      GetVideoCapturePixelFormatFromPixelFormat(pixel_format), frame_width,
      frame_height);
}

}  // namespace mri
