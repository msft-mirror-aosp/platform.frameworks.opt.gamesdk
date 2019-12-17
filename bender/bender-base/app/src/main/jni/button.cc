//
// Created by theoking on 11/25/2019.
//

#include "button.h"

Button::Button() : Button(0.0, 0.0, 0.0, 0.0, "") {}

Button::Button(float x_center,
               float x_extent,
               float y_center,
               float y_extent,
               const char *text) {
  setPosition(x_center, x_extent, y_center, y_extent);
  setLabel(text);

  onUp = [] {};
  onDown = [] {};
  onHold = [] {};
  updater = [] (Button&) {};
}

void Button::setLabel(std::string text) {
  default_label_ = text;
}

void Button::setPosition(float x_center, float x_extent, float y_center, float y_extent) {
  x_center_ = x_center;
  x_min_ = x_center - x_extent / 2;
  x_max_ = x_center + x_extent / 2;
  y_center_ = y_center;
  y_min_ = y_center - y_extent / 2;
  y_max_ = y_center + y_extent / 2;
}

bool Button::testHit(int x, int y) {
  int half_width = screen_width_ / 2;
  int half_height = screen_height_ / 2;
  return x > (x_min_ * half_width) + half_width && x < (x_max_ * half_width) + half_width
      && y > (y_min_ * half_height) + half_height
      && y < (y_max_ * half_height) + half_height;
}

void Button::drawButton(VkRenderPass render_pass, Font *font, Renderer *renderer) {
  std::string label = pressed_ ? "X" : default_label_;
  font->drawString(label,
                   1.25f,
                   x_center_ - (label.size() / 2 * FONT_SIZE_RATIO_X),
                   y_center_ - (FONT_SIZE_RATIO_Y),
                   renderer->getCurrentCommandBuffer(),
                   render_pass,
                   renderer->getCurrentFrame());
}