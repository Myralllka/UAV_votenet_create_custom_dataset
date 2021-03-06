#include "visualization_msgs/MarkerArray.h"
#include <DetectionNode.h>

/* every nodelet must include macros which export the class as a nodelet plugin */

namespace artifacts_detection {

/* onInit() method //{ */
    void DetectionNode::onInit() {

        // | ---------------- set my booleans to false ---------------- |
        // but remember, always set them to their default value in the header file
        // because, when you add new one later, you might forger to come back here

        /* obtain node handle */
        ros::NodeHandle nh = nodelet::Nodelet::getMTPrivateNodeHandle();

        /* waits for the ROS to publish clock */
        ros::Time::waitForValid();

        // | ------------------- load ros parameters ------------------ |
        /* (mrs_lib implementation checks whether the parameter was loaded or not) */
        int num_of_obj;
        mrs_lib::ParamLoader pl(nh, "DetectionNode");
        pl.loadParam("UAV_NAME", m_uav_name);
        pl.loadParam("WORLD_FRAME", m_world_frame);
        pl.loadParam("LIDAR_FRAME", m_lidar_frame);
        pl.loadParam("human_walking_positions", m_human_positions);
        pl.loadParam("number_of_humans", num_of_obj);
        pl.loadParam("human/size", m_human_size);
        pl.loadParam("human/offset", m_human_offset);

        if (!pl.loadedSuccessfully()) {
            ROS_ERROR("[DetectionNode]: failed to load non-optional parameters!");
            ros::shutdown();
        } else {
            ROS_INFO("[DetectionNode]: loaded non-optional parameters");
        }

        // As far as 'objects' are as one-dim array, read them carefully

        int counter = 0;

        for (int i = 0; i < num_of_obj; ++i) {
            geometry_msgs::Point point_for_reading;
            point_for_reading.x = m_human_positions[counter++] + m_human_offset[0];
            point_for_reading.y = m_human_positions[counter++] + m_human_offset[1];
            point_for_reading.z = m_human_positions[counter++] + m_human_offset[2];
            m_geom_markers.push_back(point_for_reading);
        }
        ROS_INFO("[DetectionNode]: readed positions to an array");

        m_pub_cube_array = nh.advertise<visualization_msgs::MarkerArray>("visualization_marker", 1);
        m_pub_pc_transformed = nh.advertise<sensor_msgs::PointCloud2>("pc_transformed", 1);
        m_pub_pc_with_metadata = nh.advertise<mrs_detection::MetadataArray>("pc_cloud_metadata", 1);

        m_sub_pc = nh.subscribe("/uav1/os_cloud_nodelet/points", 1, &DetectionNode::m_callb_pc_metadata_publish, this);
        m_sub_pc = nh.subscribe("/uav1/os_cloud_nodelet/points", 1, &DetectionNode::m_callb_pc_publish, this);
        // | --------------------- tf transformer --------------------- |

        m_transformer = mrs_lib::Transformer("DetectionNode");

        // | -------------------- initialize timers ------------------- |

        m_timer_marker = nh.createTimer(ros::Duration(0.1), &DetectionNode::tim_markers_publish, this);
        //m_timer_obj_positions = nh.createTimer(ros::Duration(1), &DetectionNode::tim_boundbox_write, this);

        ROS_INFO_ONCE("[DetectionNode]: initialized");
        is_initialized = true;
    }
//}

// | ---------------------- msg callbacks --------------------- |
    void DetectionNode::m_callb_pc_metadata_publish(const sensor_msgs::PointCloud2::ConstPtr &msg) {
        if (not is_initialized) return;
        // get transformer
        const auto tf_subt_ouster = m_transformer.getTransform(m_world_frame, m_lidar_frame, ros::Time::now());

        if (not tf_subt_ouster.has_value()) {
            ROS_INFO_THROTTLE(1.0, "[DetectionNode] No transformation form %s to %s",
                              m_world_frame.c_str(),
                              m_lidar_frame.c_str());
            return;
        }

        auto metadata_array_message = boost::make_shared<mrs_detection::MetadataArray>();
        metadata_array_message->pc = *msg.get();
        metadata_array_message->header = msg->header;

        for (size_t i = 0; i < m_human_positions.size(); ++i) {
            mrs_detection::MetadataArray metadata_message{};
        }

    }

    void DetectionNode::m_callb_pc_publish(const pcl::PointCloud<pcl::PointXYZ>::ConstPtr &msg) {
        if (not is_initialized) return;
        const auto tf_subt_ouster = m_transformer.getTransform(m_lidar_frame, m_world_frame, ros::Time::now());

        if (not tf_subt_ouster.has_value()) {
            ROS_INFO_THROTTLE(1.0, "[DetectionNode] No transformation form %s to %s",
                              m_lidar_frame.c_str(),
                              m_world_frame.c_str());
            return;
        }
        const auto transformation_mat = tf_subt_ouster->getTransformEigen();
//        auto transform_result = m_transformer.transform(tf_subt_ouster.value(), msg);
//        if (transform_result.has_value()) {
//            m_pub_pc_transformed.publish(transform_result.value());
//            ROS_INFO_THROTTLE(1.0, "[DetectionNode] transformed pc published");
//            return;
//        }
        ROS_ERROR_THROTTLE(1.0,
                           "[DetectionNode] unsuccessful transformation of pointcloud from \"%s\" to \"%s\"",
                           m_lidar_frame.c_str(),
                           m_world_frame.c_str());
    }

// | --------------------- timer callbacks -------------------- |
    void DetectionNode::tim_markers_publish([[maybe_unused]] const ros::TimerEvent &ev) {
        if (not is_initialized) return;

        visualization_msgs::MarkerArray marker_array;

        visualization_msgs::Marker marker;
        marker.header.stamp = ros::Time();
        marker.header.frame_id = m_world_frame;
        marker.ns = "mnspace";
        marker.id = 0;
        marker.type = visualization_msgs::Marker::CUBE_LIST;
        marker.action = visualization_msgs::Marker::ADD;
        std::vector<geometry_msgs::Point> markers_transformed;
        for (auto &point: m_geom_markers) {
            marker.points.push_back(point);
        }
        marker.scale.x = 1.0;
        marker.scale.y = 1.0;
        marker.scale.z = 3.0;
        marker.color.a = 0.3;
        marker.color.g = 1.0;
        marker_array.markers.push_back(marker);

        m_pub_cube_array.publish(marker_array);
        ROS_INFO_THROTTLE(1.0, "[DetectionNode] marker cube array sent");
    }


    void DetectionNode::tim_boundbox_write([[maybe_unused]] const ros::TimerEvent &ev) {
        if (not is_initialized) return;

        const auto tf_subt_ouster = m_transformer.getTransform("uav1/stable_origin", "uav1/os_lidar",
                                                               ros::Time::now());

        if (not tf_subt_ouster.has_value()) {
            ROS_INFO_THROTTLE(1.0, "[DetectionNode] No transformation form %s to %s", "subt", "uav1/os_lidar");
            return;
        }

        const auto &transform_stamped = tf_subt_ouster.value();
        std::vector<geometry_msgs::Point> points;
        for (auto &point: m_geom_markers) {
            points.push_back(m_transformer.transformHeaderless(transform_stamped, point).value());
            //marker.points.push_back(point);
        }
        for (auto &p: points) {
            std::cout << "[DetectionNode] human center: " << p << std::endl;
        }
    }
// | -------------------- other functions ------------------- |

    //}
}  // namespace artifacts_detection

/* every nodelet must export its class as nodelet plugin */
PLUGINLIB_EXPORT_CLASS(artifacts_detection::DetectionNode, nodelet::Nodelet)
