// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_AUTOFILL_MOCK_AUTOFILL_DIALOG_CONTROLLER_H_
#define CHROME_BROWSER_UI_AUTOFILL_MOCK_AUTOFILL_DIALOG_CONTROLLER_H_

#include "chrome/browser/ui/autofill/autofill_dialog_controller.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill {

class MockAutofillDialogController : public AutofillDialogController {
 public:
  MockAutofillDialogController();
  virtual ~MockAutofillDialogController();

  MOCK_CONST_METHOD0(DialogTitle, string16());
  MOCK_CONST_METHOD0(AccountChooserText, string16());
  MOCK_CONST_METHOD0(SignInLinkText, string16());
  MOCK_CONST_METHOD0(EditSuggestionText, string16());
  MOCK_CONST_METHOD0(CancelButtonText, string16());
  MOCK_CONST_METHOD0(ConfirmButtonText, string16());
  MOCK_CONST_METHOD0(SaveLocallyText, string16());
  MOCK_METHOD0(LegalDocumentsText, string16());
  MOCK_CONST_METHOD0(SignedInState, DialogSignedInState());
  MOCK_CONST_METHOD0(ShouldShowSpinner, bool());
  MOCK_CONST_METHOD0(ShouldOfferToSaveInChrome, bool());
  MOCK_METHOD0(MenuModelForAccountChooser, ui::MenuModel*());
  MOCK_METHOD0(AccountChooserImage, gfx::Image());
  MOCK_CONST_METHOD0(ShouldShowProgressBar, bool());
  MOCK_CONST_METHOD0(GetDialogButtons, int());
  MOCK_CONST_METHOD0(ShouldShowDetailArea, bool());
  MOCK_CONST_METHOD1(IsDialogButtonEnabled, bool(ui::DialogButton button));
  MOCK_CONST_METHOD0(GetDialogOverlay, DialogOverlayState());
  MOCK_METHOD0(LegalDocumentLinks, const std::vector<ui::Range>&());
  MOCK_CONST_METHOD1(SectionIsActive, bool(DialogSection));
  MOCK_CONST_METHOD1(RequestedFieldsForSection,
                     const DetailInputs&(DialogSection));
  MOCK_METHOD1(ComboboxModelForAutofillType,
               ui::ComboboxModel*(AutofillFieldType));
  MOCK_METHOD1(MenuModelForSection, ui::MenuModel*(DialogSection));
  MOCK_CONST_METHOD1(LabelForSection, string16(DialogSection section));
  MOCK_METHOD1(SuggestionStateForSection, SuggestionState(DialogSection));
  MOCK_METHOD1(EditClickedForSection, void(DialogSection section));
  MOCK_METHOD1(EditCancelledForSection, void(DialogSection section));
  MOCK_CONST_METHOD2(IconForField,
                     gfx::Image(AutofillFieldType, const string16&));
  MOCK_METHOD3(InputValidityMessage,
      string16(DialogSection, AutofillFieldType, const string16&));
  MOCK_METHOD3(InputsAreValid, ValidityData(DialogSection,
                                            const DetailOutputMap&,
                                            ValidationType));
  MOCK_METHOD6(UserEditedOrActivatedInput,void(DialogSection,
                                               const DetailInput*,
                                               gfx::NativeView,
                                               const gfx::Rect&,
                                               const string16&,
                                               bool was_edit));
  MOCK_METHOD1(HandleKeyPressEventInInput,
               bool(const content::NativeWebKeyboardEvent& event));
  MOCK_METHOD0(FocusMoved, void());
  MOCK_CONST_METHOD0(SplashPageImage, gfx::Image());
  MOCK_METHOD0(ViewClosed, void());
  MOCK_METHOD0(CurrentNotifications,std::vector<DialogNotification>());
  MOCK_CONST_METHOD0(CurrentAutocheckoutSteps,
                     std::vector<DialogAutocheckoutStep>());
  MOCK_METHOD0(SignInLinkClicked, void());
  MOCK_METHOD2(NotificationCheckboxStateChanged,
               void(DialogNotification::Type, bool));
  MOCK_METHOD1(LegalDocumentLinkClicked, void(const ui::Range&));
  MOCK_METHOD0(OverlayButtonPressed, void());
  MOCK_METHOD0(OnCancel, bool());
  MOCK_METHOD0(OnAccept, bool());
  MOCK_METHOD0(profile, Profile*());
  MOCK_METHOD0(web_contents, content::WebContents*());
 private:
  DetailInputs default_inputs_;
  DetailInputs cc_default_inputs_;  // Default inputs for SECTION_CC.
  std::vector<ui::Range> range_;
};

}  // namespace autofill

#endif  // CHROME_BROWSER_UI_AUTOFILL_MOCK_AUTOFILL_DIALOG_CONTROLLER_H_
