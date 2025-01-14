// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_MESSAGE_CENTER_VIEWS_MESSAGE_CENTER_VIEW_H_
#define UI_MESSAGE_CENTER_VIEWS_MESSAGE_CENTER_VIEW_H_

#include "ui/views/view.h"

#include "ui/base/animation/animation_delegate.h"
#include "ui/message_center/message_center_export.h"
#include "ui/message_center/message_center_observer.h"
#include "ui/message_center/notification_list.h"
#include "ui/views/controls/button/button.h"

namespace ui {
class MultiAnimation;
}  // namespace ui

namespace views {
class Button;
}  // namespace views

namespace message_center {

class MessageCenter;
class MessageCenterBubble;
class NotificationCenterButton;
class MessageCenterButtonBar;
class MessageCenterTray;
class MessageCenterView;
class MessageView;
class MessageListView;
class NotifierSettingsView;

// MessageCenterView ///////////////////////////////////////////////////////////

class MESSAGE_CENTER_EXPORT MessageCenterView : public views::View,
                                                public MessageCenterObserver,
                                                public ui::AnimationDelegate {
 public:
  MessageCenterView(MessageCenter* message_center,
                    MessageCenterTray* tray,
                    int max_height,
                    bool initially_settings_visible,
                    bool top_down);
  virtual ~MessageCenterView();

  void SetNotifications(const NotificationList::Notifications& notifications);

  void ClearAllNotifications();
  void OnAllNotificationsCleared();

  size_t NumMessageViewsForTest() const;

  void SetSettingsVisible(bool visible);
  bool settings_visible() const { return settings_visible_; }

 protected:
  // Overridden from views::View:
  virtual void Layout() OVERRIDE;
  virtual gfx::Size GetPreferredSize() OVERRIDE;
  virtual int GetHeightForWidth(int width) OVERRIDE;
  virtual bool OnMouseWheel(const ui::MouseWheelEvent& event) OVERRIDE;
  virtual void OnMouseExited(const ui::MouseEvent& event) OVERRIDE;

  // Overridden from MessageCenterObserver:
  virtual void OnNotificationAdded(const std::string& id) OVERRIDE;
  virtual void OnNotificationRemoved(const std::string& id,
                                     bool by_user) OVERRIDE;
  virtual void OnNotificationUpdated(const std::string& id) OVERRIDE;

  // Overridden from ui::AnimationDelegate:
  virtual void AnimationEnded(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationProgressed(const ui::Animation* animation) OVERRIDE;
  virtual void AnimationCanceled(const ui::Animation* animation) OVERRIDE;

 private:
  friend class MessageCenterViewTest;

  void AddNotificationAt(const Notification& notification, int index);
  void NotificationsChanged();
  void SetNotificationViewForTest(views::View* view);

  MessageCenter* message_center_;  // Weak reference.
  MessageCenterTray* tray_;  // Weak reference.
  std::vector<MessageView*> message_views_;
  views::ScrollView* scroller_;
  MessageListView* message_list_view_;
  NotifierSettingsView* settings_view_;
  MessageCenterButtonBar* button_bar_;
  views::View* no_notifications_message_view_;
  bool top_down_;

  // Data for transition animation between settings view and message list.
  bool settings_visible_;
  views::View* source_view_;
  views::View* target_view_;
  int source_height_;
  int target_height_;
  scoped_ptr<ui::MultiAnimation> settings_transition_animation_;

  DISALLOW_COPY_AND_ASSIGN(MessageCenterView);
};

}  // namespace message_center

#endif  // UI_MESSAGE_CENTER_VIEWS_MESSAGE_CENTER_VIEW_H_
