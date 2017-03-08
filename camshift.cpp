#include "camshift.h"
#include "widget.h"

#include <AnnotatorLib/Annotation.h>
#include <AnnotatorLib/Commands/NewAnnotation.h>
#include <AnnotatorLib/Frame.h>
#include <AnnotatorLib/Session.h>
#include <QDebug>
#include <QtGui/QPainter>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <ctype.h>
#include <iostream>

using namespace Annotator::Plugins;

Annotator::Plugins::CamShift::CamShift() {
  hranges[0] = 0.0;
  hranges[1] = 180.0;
  phranges = hranges;
}

CamShift::~CamShift() {}

QString CamShift::getName() { return "CamShift"; }

QWidget *CamShift::getWidget() { return &widget; }

bool CamShift::setFrame(shared_ptr<Frame> frame, cv::Mat image) {
  this->lastFrame = this->frame;
  this->frame = frame;
  this->frameImg = image;
  return lastFrame != frame;
}

// first call
void CamShift::setObject(shared_ptr<Object> object) {
  if (object != this->object) {
    this->object = object;
    this->trackerStarted = false;
  }
}

shared_ptr<Object> CamShift::getObject() const { return object; }

// second call
void CamShift::setLastAnnotation(shared_ptr<Annotation> annotation) {
  if (trackerStarted || annotation == nullptr ||
      annotation->getObject() != object)
    return;
  if (lastAnnotation != nullptr &&
      annotation->getObject() == lastAnnotation->getObject())
    return;

  this->lastAnnotation = annotation;
  selection = cv::Rect(lastAnnotation->getX(), lastAnnotation->getY(),
                       lastAnnotation->getWidth(), lastAnnotation->getHeight());
  this->lastFrame = this->lastAnnotation->getFrame();
  newSelection = true;
  trackerStarted = true;
}

std::vector<shared_ptr<Commands::Command>> CamShift::getCommands() {
  std::vector<shared_ptr<Commands::Command>> commands;
  if (object == nullptr || frame == nullptr || lastFrame == nullptr ||
      this->lastAnnotation == nullptr || frame == lastFrame)
    return commands;

  try {
    cv::Rect found_rect = findObject();

    int w = found_rect.width;
    int h = found_rect.height;

    int x = found_rect.x;
    int y = found_rect.y;

    shared_ptr<Commands::NewAnnotation> nA =
        std::make_shared<Commands::NewAnnotation>(getProject()->getSession(),
                                                  lastAnnotation->getObject(),
                                                  frame, x, y, w, h);
    commands.push_back(nA);

  } catch (std::exception &e) {
    qDebug() << e.what();
  }

  return commands;
}

cv::Rect CamShift::findObject() {
  cv::cvtColor(frameImg, hsv, cv::COLOR_BGR2HSV);
  cv::inRange(hsv, cv::Scalar(0, smin, std::min(vmin, vmax)),
              cv::Scalar(180, 256, std::max(vmin, vmax)), mask);

  int ch[2] = {0, 0};

  hue.create(hsv.size(), hsv.depth());
  cv::mixChannels(&hsv, 1, &hue, 1, ch, 1);

  if (newSelection) {
    selection &= cv::Rect(0, 0, frameImg.cols, frameImg.rows);
    cv::Mat roi(hue, selection), maskroi(mask, selection);
    cv::calcHist(&roi, 1, 0, maskroi, hist, 1, &hsize, &phranges);
    cv::normalize(hist, hist, 0, 255, CV_MINMAX);
    trackWindow = selection;
    newSelection = false;
  }

  cv::calcBackProject(&hue, 1, 0, hist, backproj, &phranges);

  backproj &= mask;

  cv::TermCriteria criteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 10,
                            1);

  cv::RotatedRect found_object = cv::CamShift(backproj, trackWindow, criteria);

  // debug
  cv::ellipse(frameImg, found_object, cv::Scalar(0, 0, 255), 3, CV_AA);
  // cv::rectangle(frameImg, found_object.boundingRect(), cv::Scalar(0,0,255),
  // 3, CV_AA);
  // cv::imshow("CamShift Debug", frameImg);

  return found_object.boundingRect();
}
