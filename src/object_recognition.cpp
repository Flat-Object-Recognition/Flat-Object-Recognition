#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/objdetect/objdetect.hpp"
#include "opencv2\nonfree\features2d.hpp"
#include "opencv2\calib3d\calib3d.hpp"

#include <fstream>
#include <iostream>

#include "ctime"
#include "featureExtractor.hpp"

using namespace cv;
using namespace std;


#if 1
  #define TS(name) int64 t_##name = getTickCount()
  #define TE(name) printf("TIMER_" #name ": %.2fms\n", \
    1000.f * ((getTickCount() - t_##name) / getTickFrequency()))
#else
  #define TS(name)
  #define TE(name)
#endif

const char* params =
     "{ h | help             | false | print usage                                   }"
     "{   | sample-list      |       | path to list with image classes names         }"
     "{   | samples          |       | path to samples                               }"
     "{   | image            |       | image to detect objects on                    }"
     "{   | camera           | false | whether to detect on video stream from camera }"
	 "{   | object-to-camera |       | object from camera                            }"
	 "{   | new-class-object | false | flag for add new class object                 }"
	 "{   | new-image        |       | add new object                                }"
	 "{   | new-class-image  |       | add new class object                          }";



bool onSameSide(Point2f p1, Point2f p2, Point2f p3, Point2f p4) {
	float k =0;
	if (p1.x!=p2.x) k = (p1.y-p2.y)/(p1.x-p2.x); else k = (p1.y-p2.y)/(p1.x-p2.x+0.0001);
	float b = p1.y - k*p1.x;
	if (((p3.y-k*p3.x-b)>0 && (p4.y-k*p4.x-b)>0) || ((p3.y-k*p3.x-b)<0 && (p4.y-k*p4.x-b)<0)) return true; else return false;
}

bool isConvex(Point2f p0, Point2f p1, Point2f p2, Point2f p3) {	
	if ( onSameSide(p0, p1, p2, p3) && 
		onSameSide(p1, p2, p3, p0) && 
		onSameSide(p2, p3, p0, p1) && 
		onSameSide(p3, p0, p1, p2)
		) return true; else return false;
	
}

void subscribeObject(Mat& image, string name, Point2f leftCornerCoord)
{    
    Point2f textCoord(leftCornerCoord.x, leftCornerCoord.y + 25);
    Scalar Red(0, 0, 255);
	putText(image, name, textCoord, FONT_ITALIC, 1, Red, 2,5);
}


float calculateTriangleArea(Point2f p1, Point2f p2, Point2f p3) {
	float a = sqrt((p1.x-p2.x)*(p1.x-p2.x) + (p1.y-p2.y)*(p1.y-p2.y));
	float b = sqrt((p3.x-p2.x)*(p3.x-p2.x) + (p3.y-p2.y)*(p3.y-p2.y));
	float c = sqrt((p1.x-p3.x)*(p1.x-p3.x) + (p1.y-p3.y)*(p1.y-p3.y));
	float p = (a+b+c)/2;
	float area = sqrt (p*(p-a)*(p-b)*(p-c));
	return area;
}


float fourPointsArea(Point2f p1, Point2f p2, Point2f p3, Point2f p4) {
	float tr1 = calculateTriangleArea(p1,p2,p3);
	float tr2 = calculateTriangleArea(p1,p3,p4);
	return tr1+tr2;
}

void DrawContours(const Mat image, Mat& test_image, const Mat homography, Scalar color, string objectName ) {
	std::vector<Point2f> startcorners, newcorners;
	std::vector<float> distances;
	startcorners.push_back(Point2f(0,0));
	startcorners.push_back(Point2f(image.cols,0));
	startcorners.push_back(Point2f( image.cols, image.rows));
	startcorners.push_back(Point2f( 0, image.rows));

	perspectiveTransform(startcorners, newcorners, homography);

	float areaOrig = fourPointsArea(startcorners[0], startcorners[1], startcorners[2], startcorners[3]);
	float areaFound = fourPointsArea(newcorners[0], newcorners[1], newcorners[2], newcorners[3]);
	if (areaFound/areaOrig>0.2 && isConvex(newcorners[0], newcorners[1], newcorners[2], newcorners[3]))
	{
	    line(test_image, Point2f(newcorners[0].x, newcorners[0].y), Point2f(newcorners[1].x, newcorners[1].y), color, 4);
	    line(test_image, Point2f(newcorners[1].x, newcorners[1].y), Point2f(newcorners[2].x, newcorners[2].y), color, 4);
	    line(test_image, Point2f(newcorners[2].x, newcorners[2].y), Point2f(newcorners[3].x, newcorners[3].y), color, 4);
	    line(test_image, Point2f(newcorners[3].x, newcorners[3].y), Point2f(newcorners[0].x, newcorners[0].y), color, 4);
		
		subscribeObject(test_image, objectName, newcorners[0]);

	}	
}


void compute(Mat &image, featureExtractor &extractor)
{
	extractor.compute(image);
}

vector<DMatch> matches(featureExtractor& object, featureExtractor& test)
{
	BFMatcher matcher( NORM_L1 );
	vector< DMatch > match;
	matcher.match( object.GetDescriptor() , test.GetDescriptor() ,match );
	
	return match;

}

Mat Homography(vector<DMatch> matches,featureExtractor &object, featureExtractor &test, double ransacThreshold, Mat& H )
{
	vector<Point2f> obj;
	vector<Point2f> scene;
	for( int i = 0; i < matches.size(); i++ )
	{
		obj.push_back( object.GetKeyPoint()[i].pt );
		scene.push_back(  test.GetKeyPoint()[ matches[i].trainIdx ].pt ); 
	}
	H =  findHomography ( Mat(obj), Mat(scene), CV_RANSAC, ransacThreshold);
	Mat scene_corners;
	perspectiveTransform(Mat(obj), scene_corners, H);
	return scene_corners;

}

void inliers(vector< DMatch > matches, Mat &scene_corners,featureExtractor &test, double ransacThreshold, vector<DMatch>  &inliers)
{
	for (int i=0; i<matches.size(); i++)
	{
		Point2f p1 = test.GetKeyPoint().at( matches[i].trainIdx ).pt;
		Point2f p2 = scene_corners.at<Point2f>(matches[i].queryIdx);
		if ((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y) < 
			ransacThreshold * ransacThreshold)
		{
			inliers.push_back(matches[i]);
		}
	}
}

void Add_Class(string path_to_image, string name_class, string sampleListFile,string path)
{
	std::ofstream   sampleListFileReader(sampleListFile,std::ofstream::app);
	Mat image = imread(path_to_image);
	imwrite(path + "\data\12.jpg",image); 
	string str = "/data/12.jpg " + name_class +"\n" ; 
	sampleListFileReader << str;
	sampleListFileReader.close();

}

int main(int argc, const char **argv)
{ 
	vector<SurfDescriptorExtractor> detector;
	Mat image, test_image;
    CommandLineParser parser(argc, argv, params);
	
	string sampleListFile = parser.get<string>("sample-list");
	string testImage = parser.get<string>("image");
	string _path = parser.get<string>("samples");

	bool use_camera = parser.get<bool>("camera");

	if (use_camera)
	{
		VideoCapture cap(0);
		Mat frame, scene,H;

		featureExtractor test, tmp;
		vector< DMatch > matcher, inlier;
		Mat object = imread( parser.get<string>("object-to-camera"));
		compute(object,tmp);
		while (true)
		{
			cap.read(frame);
			if (!frame.empty())
			{
				//imshow("image",frame);
				//waitKey();
				TS();
				compute(frame,test);
				//matcher = matches(tmp, test);
				//drawMatches(object, tmp.GetKeyPoint(),frame, test.GetKeyPoint(),inlier,image);
				//imshow("matches",image);
				//waitKey();

				scene = Homography(	matches(tmp, test), tmp, test, 3.0, H);

				//inliers(matcher,scene,test,3.0,inlier);

				//drawMatches(object, tmp.GetKeyPoint(),frame, test.GetKeyPoint(),inlier,image);
				//imshow("matches",image);
				//waitKey();
				TE();
				DrawContours(object, frame, H, Scalar(0,255,0), "our_object");
				imshow("image",frame);
				if(waitKey(27) >= 0) break;
			}
			
		}
		return 1;
	}

	bool add_new_class = parser.get<bool>("new-class-object");
	if (add_new_class)
	{
		Add_Class( parser.get<string>("new-image") , parser.get<string>("new-class-image") , sampleListFile,_path);
		
	}
    
    std::ifstream sampleListFileReader(sampleListFile);
    char buff[50];
	test_image = imread(testImage);
	if (test_image.empty())
	{
		std::cout << "Image is empty." << std::endl;
		return 1;
	}	
	TS();
	featureExtractor test,
		tmp;

	compute(test_image,test);

	Mat H,
		scene,
		img;

	vector< DMatch > matcher, inlier;
	string str;

    while (sampleListFileReader.getline(buff, 50))
    {
        str = (string) buff;
        string image_file = str.substr(0,str.find(" "));
		image = imread(_path + image_file);
		compute(image,tmp);
		matcher = matches(tmp, test);
		scene = Homography(	matches(tmp, test), tmp, test, 3.0, H);
		inliers(matcher,scene,test,3.0,inlier);
		//drawMatches(image, tmp.GetKeyPoint(),test_image, test.GetKeyPoint(),inlier,img);
		
		//waitKey();
		str = str.substr(str.find(" "), str.length()-1);
		DrawContours(image, test_image, H, Scalar(0,255,0), str);	
    }
//	drawMatches(image, tmp.GetKeyPoint(),test_image, test.GetKeyPoint(),inlier,img);
    //imshow("image_matches",img);
	TE();
	imshow("image",test_image);
	waitKey();
    return 0;
}