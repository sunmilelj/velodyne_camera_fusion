/*

*/

#include <ros/ros.h>

#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>

#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>
#include <pcl/visualization/common/float_image_utils.h>

#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>

#include <image_transport/image_transport.h>
#include <image_geometry/pinhole_camera_model.h>

#include <tf/tf.h>
#include <tf/transform_listener.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <point_coloring.h>

template<typename T_p>
class Projection{
    private:
        ros::NodeHandle nh;

        typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::Image, sensor_msgs::CameraInfo, sensor_msgs::PointCloud2> sensor_fusion_sync_subs;
        message_filters::Subscriber<sensor_msgs::Image> image_sub;
        message_filters::Subscriber<sensor_msgs::CameraInfo> cinfo_sub;
        message_filters::Subscriber<sensor_msgs::PointCloud2> lidar_sub;
        message_filters::Synchronizer<sensor_fusion_sync_subs> sensor_fusion_sync;

        ros::Publisher pub;
        ros::Publisher pub_;

        tf::TransformListener listener;
        tf::StampedTransform  transform;
        bool flag;

    public:
  //コンストラクタ
        Projection();
        void Callback(const sensor_msgs::Image::ConstPtr&, const sensor_msgs::CameraInfo::ConstPtr&, const sensor_msgs::PointCloud2::ConstPtr&);
        void projection(const sensor_msgs::Image::ConstPtr, const sensor_msgs::CameraInfo::ConstPtr, const sensor_msgs::PointCloud2::ConstPtr);
        bool tflistener(std::string target_frame, std::string source_frame);
};

template<typename T_p>
Projection<T_p>::Projection()
    : nh("~"),
      image_sub(nh, "/image", 10), cinfo_sub(nh, "/camera_info", 10), lidar_sub(nh, "/lidar", 10),
      sensor_fusion_sync(sensor_fusion_sync_subs(10), image_sub, cinfo_sub, lidar_sub)
{
    ROS_INFO("now constract function starting");

    sensor_fusion_sync.registerCallback(boost::bind(&Projection::Callback, this, _1, _2, _3));
    pub = nh.advertise<sensor_msgs::Image>("/projection", 10);
    pub_ = nh.advertise<sensor_msgs::PointCloud2>("/fusion_points", 10);
    ROS_INFO("advertised /projection");
    flag = false;
}


// callback
template<typename T_p>
void Projection<T_p>::Callback(const sensor_msgs::Image::ConstPtr& image,
                             const sensor_msgs::CameraInfo::ConstPtr& cinfo,
                             const sensor_msgs::PointCloud2::ConstPtr& pc2)
{
    ROS_INFO("Call back start");
    if(!flag) tflistener("occam/image0", "/velodyne");
    projection(image, cinfo, pc2);
}

// transform listener
template<typename T_p>
bool Projection<T_p>::tflistener(std::string target_frame, std::string source_frame)
{
    ros::Time time = ros::Time(0);
    try{
      //カメラ座標とLiDAR座標が利用できるようになるまで待ち、その後LiDAR座標をカメラ座標へ追従させる変換を行う
        listener.waitForTransform(target_frame, source_frame, time, ros::Duration(4.0));
        listener.lookupTransform(target_frame, source_frame,  time, transform);
	ROS_INFO("Transform OK");
        return true;
    }
    catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
        ros::Duration(4.0).sleep();
        return false;
    }
}

// template<typename T_p>
// bool Projection<T_p>::tflisteners(std::string target_frame, std::string source_frame)
// {
//   ros::Time time = ros::Time(0);
//   try{
//     listener.waitForTransform(target_frame, source_frame, time, ros::Duration(4.0));
//     listener.lookupTransform(target_frame, source_frame, time, transform);
//     ROS_INFO("Transform OK");
//     return true;
//   }
//   catch (tf::TransformException ex){
//     ROS_ERROR("%s", ex.what());
//     ros::Duration(4.0).sleep();
//     return false;
//   }
// }
// main function
template<typename T_p>
void Projection<T_p>::projection(const sensor_msgs::Image::ConstPtr image,
                                 const sensor_msgs::CameraInfo::ConstPtr cinfo,
                                 const sensor_msgs::PointCloud2::ConstPtr pc2)
{
    typename pcl::PointCloud<T_p>::Ptr cloud(new pcl::PointCloud<T_p>);
    //LiDARのpclデータをROS用に変換
    pcl::fromROSMsg(*pc2, *cloud);

    // transform pointcloud from lidar_frame to camera_frame
    tf::Transform tf;
    //tfの座標及び回転を準備
    tf.setOrigin(transform.getOrigin());
    tf.setRotation(transform.getRotation());
    typename pcl::PointCloud<T_p>::Ptr trans_cloud(new pcl::PointCloud<T_p>);
    //tfの情報を元にLiDARの点群データを変換して出力する
    pcl_ros::transformPointCloud(*cloud, *trans_cloud, tf);

    //cv_bridge
    std::cout<<"cv_bridge"<<std::endl;
    cv_bridge::CvImageConstPtr cv_img_ptr;
    try{
        cv_img_ptr = cv_bridge::toCvShare(image);
    }catch (cv_bridge::Exception& e){
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return;
    }
    cv::Mat cv_image(cv_img_ptr->image.rows, cv_img_ptr->image.cols, cv_img_ptr->image.type());
    cv_image = cv_bridge::toCvShare(image)->image;

    // Realsense Data is saved BGR. change BGR to RGB
    cv::Mat rgb_image;
    cv::cvtColor(cv_image ,rgb_image, CV_BGR2RGB);

    // set PinholeCameraModel
    image_geometry::PinholeCameraModel cam_model;
    cam_model.fromCameraInfo(cinfo);
    int i = 0;
    int j = 0;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr msg (new pcl::PointCloud<pcl::PointXYZRGB>);
    msg->header.frame_id = "occam/image0";
    msg->points.resize(20000);

    // for(typename pcl::PointCloud<T_p>::iterator pt=trans_cloud->points.begin(); pt<trans_cloud->points.end(); pt++)
    // {
    //   if((*pt).z<0) continue;

    //     cv::Point3d pt_cv((*pt).x, (*pt).y, (*pt).z);
    //     cv::Point2d uv;
    // 	//LiDARから得られる点群をピンホールカメラモデルによって2次元に変換し格納する
    //     uv = cam_model.project3dToPixel(pt_cv);

    //     if(uv.x>0 && uv.x < rgb_image.cols && uv.y > 0 && uv.y < rgb_image.rows)
    //     {
    // 	  //LiDARから得られる点の距離を計算する
    //         double range = sqrt( pow((*pt).x, 2.0) + pow((*pt).y, 2.0) + pow((*pt).z, 2.0));
    // 	    //点の距離に応じて色付けを変える
    //         COLOUR c = GetColour(int (range/20*255.0), 0, 255);
    // 	    //カメラ画像について，点群と画像中で対応する部分に色付き点(円)を表示する
    //         cv::circle(rgb_image, uv, 10, cv::Scalar(int(255*c.b),int(255*c.g),int(255*c.r)), -1);
    //         // rgb_image.at<cv::Vec3b>(uv)[0] = 255*c.r;
    //         // rgb_image.at<cv::Vec3b>(uv)[1] = 255*c.g;
    //         // rgb_image.at<cv::Vec3b>(uv)[2] = 255*c.b;

    //     }
    // 	else
    //     {
    // 	  i++;
    // 	  if (i < 50)
    // 	  {
    // 	    ROS_INFO("x: %f", (*pt).x);
    // 	    ROS_INFO("y: %f", (*pt).y);
    // 	    ROS_INFO("z: %f", (*pt).z);
    // 	    ROS_INFO("camera_x: %f", uv.x);
    // 	    ROS_INFO("camera_y: %f", uv.y);
    // 	  }
    // 	}
    // }
    // ROS_INFO("Pointcloud processing complete");
    // sensor_msgs::ImagePtr projection_image;
    // projection_image = cv_bridge::CvImage(std_msgs::Header(), "bgr8", rgb_image).toImageMsg();
    // projection_image->header.frame_id = image->header.frame_id;
    // projection_image->header.stamp = ros::Time::now();
    // pub.publish(projection_image);

    // Projection Step
    for(typename pcl::PointCloud<T_p>::iterator pt=trans_cloud->points.begin(); pt<trans_cloud->points.end(); pt++)
    {
      if((*pt).z<0) continue;

        cv::Point3d pt_cv((*pt).x, (*pt).y, (*pt).z);
        cv::Point2d uv;
	//LiDARから得られる点群をピンホールカメラモデルによって2次元に変換し格納する
        uv = cam_model.project3dToPixel(pt_cv);

        if (uv.x>(-rgb_image.cols/2) && uv.x < (rgb_image.cols/2) && uv.y > (-rgb_image.rows/2) && uv.y < (rgb_image.rows/2))
        {
	  //LiDARから得られる点の距離を計算する
            double range = sqrt( pow((*pt).x, 2.0) + pow((*pt).y, 2.0) + pow((*pt).z, 2.0));
	    //点の距離に応じて色付けを変える
            COLOUR c = GetColour(int (range/20*255.0), 0, 255);
	    cv::Point2d uv_(uv.x + rgb_image.cols / 2, uv.y + rgb_image.rows / 2);
	    //カメラ画像について，点群と画像中で対応する部分に色付き点(円)を表示する
            cv::circle(rgb_image, uv_, 1, cv::Scalar(int(255*c.b),int(255*c.g),int(255*c.r)), -1);
	    i++;
	    if (i < 20)
	      {
		ROS_INFO("x: %f", (*pt).x);
		ROS_INFO("y: %f", (*pt).y);
		ROS_INFO("z: %f", (*pt).z);
		ROS_INFO("image_x: %f", uv_.x);
		ROS_INFO("image_y: %f", uv_.y);
	      }
            // rgb_image.at<cv::Vec3b>(uv)[0] = 255*c.r;
            // rgb_image.at<cv::Vec3b>(uv)[1] = 255*c.g;
            // rgb_image.at<cv::Vec3b>(uv)[2] = 255*c.b;
	    msg->points[j].x = (*pt).x;
	    msg->points[j].y = (*pt).y;
	    msg->points[j].z = (*pt).z;
	    msg->points[j].r = int(255*c.r);
	    msg->points[j].g = int(255*c.g);
	    msg->points[j].b = int(255*c.b);
	    msg->points.push_back(msg->points[j]);
	    j++;
        }

    }
    ROS_INFO("%d Pointcloud processing complete", i);
    sensor_msgs::ImagePtr projection_image;
    projection_image = cv_bridge::CvImage(std_msgs::Header(), "bgr8", rgb_image).toImageMsg();
    projection_image->header.frame_id = image->header.frame_id;
    projection_image->header.stamp = pc2->header.stamp;
    pub.publish(projection_image);
    pcl_conversions::toPCL(ros::Time::now(), msg->header.stamp);
    pub_.publish(msg);
}


int main(int argc, char** argv)
{
    ROS_INFO("Hello world!!");

    ros::init(argc, argv, "matching_points_image");

    ROS_INFO("declare class 'Projection'");

    Projection<pcl::PointXYZI> cr;

    ros::spin();

    return 0;
}
