/**
 * Live monocular-inertial ORB-SLAM3.
 *
 * Reads from stdin:
 *   'F' + double(timestamp) + uint32(w) + uint32(h) + uint32(nbytes) + BGR pixels
 *   'I' + 7 doubles (timestamp, ax, ay, az, gx, gy, gz)
 *
 * IMU measurements are accumulated between frames and passed to TrackMonocular.
 */
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <opencv2/core/core.hpp>
#include <System.h>
#include <ImuTypes.h>

static bool read_exact(void* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(STDIN_FILENO, (char*)buf + off, n - off);
        if (r <= 0) return false;
        off += r;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./mono_live path_to_vocabulary path_to_settings [--no-viewer]" << std::endl;
        return 1;
    }

    bool use_viewer = true;
    if (argc > 3 && std::string(argv[3]) == "--no-viewer")
        use_viewer = false;

    ORB_SLAM3::System SLAM(argv[1], argv[2], ORB_SLAM3::System::IMU_MONOCULAR, use_viewer);
    float imageScale = SLAM.GetImageScale();

    int frame_idx = 0;
    double prev_timestamp = -1.0;
    std::vector<ORB_SLAM3::IMU::Point> vImuMeas;

    std::cerr << "mono_live: waiting for frames+IMU on stdin (mono-inertial)..." << std::endl;

    while (true) {
        // Read message type
        char msg_type;
        if (!read_exact(&msg_type, 1))
            break;

        if (msg_type == 'I') {
            // IMU measurement: 7 doubles
            double imu[7];
            if (!read_exact(imu, sizeof(imu)))
                break;
            // imu[0]=timestamp, imu[1..3]=accel(xyz), imu[4..6]=gyro(xyz)
            vImuMeas.push_back(ORB_SLAM3::IMU::Point(
                imu[1], imu[2], imu[3],  // ax, ay, az
                imu[4], imu[5], imu[6],  // gx, gy, gz
                imu[0]                     // timestamp
            ));
        }
        else if (msg_type == 'F') {
            // Frame: timestamp + w + h + nbytes + pixels
            double timestamp;
            if (!read_exact(&timestamp, sizeof(timestamp)))
                break;

            uint32_t header[3];
            if (!read_exact(header, sizeof(header)))
                break;

            uint32_t w = header[0], h = header[1], nbytes = header[2];
            if (nbytes != w * h * 3) {
                std::cerr << "mono_live: bad frame size" << std::endl;
                break;
            }

            cv::Mat im(h, w, CV_8UC3);
            if (!read_exact(im.data, nbytes))
                break;

            if (imageScale != 1.f) {
                cv::resize(im, im, cv::Size(im.cols * imageScale, im.rows * imageScale));
            }

            // Skip frame if no IMU data — ORB-SLAM3 crashes on empty preintegration
            size_t n_imu = vImuMeas.size();
            if (n_imu == 0 && frame_idx > 0) {
                std::cerr << "[" << frame_idx << "] SKIP  imu=0 (no IMU between frames)" << std::endl;
                frame_idx++;
                continue;
            }

            // Track with IMU
            auto t1 = std::chrono::steady_clock::now();
            try {
                SLAM.TrackMonocular(im, timestamp, vImuMeas);
            } catch (const std::exception& e) {
                std::cerr << "[" << frame_idx << "] EXCEPTION: " << e.what()
                          << "  imu=" << n_imu << std::endl;
                vImuMeas.clear();
                frame_idx++;
                continue;
            } catch (...) {
                std::cerr << "[" << frame_idx << "] UNKNOWN EXCEPTION  imu=" << n_imu << std::endl;
                vImuMeas.clear();
                frame_idx++;
                continue;
            }
            auto t2 = std::chrono::steady_clock::now();
            double dt_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

            int state = SLAM.GetTrackingState();
            const char* state_str = "UNKNOWN";
            switch (state) {
                case -1: state_str = "SYSTEM_NOT_READY"; break;
                case 0:  state_str = "NO_IMAGES"; break;
                case 1:  state_str = "NOT_INITIALIZED"; break;
                case 2:  state_str = "OK"; break;
                case 3:  state_str = "RECENTLY_LOST"; break;
                case 4:  state_str = "LOST"; break;
            }

            double frame_dt = (prev_timestamp >= 0) ? (timestamp - prev_timestamp) * 1000.0 : 0.0;
            prev_timestamp = timestamp;

            // Show IMU timestamp range vs frame timestamp for debugging
            double imu_first = (n_imu > 0) ? vImuMeas.front().t : 0;
            double imu_last = (n_imu > 0) ? vImuMeas.back().t : 0;

            std::cerr << "[" << frame_idx << "] " << state_str
                      << "  imu=" << n_imu
                      << "  proc=" << dt_ms << "ms"
                      << "  dt=" << frame_dt << "ms";
            if (frame_idx < 20 || frame_idx % 50 == 0) {
                std::cerr << "  frame_t=" << std::fixed << std::setprecision(3) << timestamp
                          << "  imu=[" << imu_first << ".." << imu_last << "]";
            }
            std::cerr << std::endl;

            vImuMeas.clear();
            frame_idx++;
        }
        else {
            std::cerr << "mono_live: unknown message type " << (int)msg_type << std::endl;
            break;
        }
    }

    std::cerr << "mono_live: " << frame_idx << " frames processed" << std::endl;
    SLAM.Shutdown();
    SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");
    return 0;
}
