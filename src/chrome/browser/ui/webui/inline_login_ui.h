// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_INLINE_LOGIN_UI_H_
#define CHROME_BROWSER_UI_WEBUI_INLINE_LOGIN_UI_H_

#include "chrome/browser/extensions/scoped_gaia_auth_extension.h"
#include "ui/web_dialogs/web_dialog_ui.h"

// Inline login WebUI in various signin flows for ChromeOS and Chrome desktop.
// The authentication is carried out via the host gaia_auth extension. Upon
// success, the profile of the webui should be populated with proper cookies.
// Then this UI would fetch the oauth2 tokens using the cookies.
class InlineLoginUI : public ui::WebDialogUI {
 public:
  explicit InlineLoginUI(content::WebUI* web_ui);
  virtual ~InlineLoginUI();

 private:
  ScopedGaiaAuthExtension auth_extension_;

  DISALLOW_COPY_AND_ASSIGN(InlineLoginUI);
};

#endif  // CHROME_BROWSER_UI_WEBUI_INLINE_LOGIN_UI_H_
