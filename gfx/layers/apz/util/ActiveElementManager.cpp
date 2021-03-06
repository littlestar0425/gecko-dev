/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ActiveElementManager.h"
#include "mozilla/EventStates.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "inIDOMUtils.h"
#include "nsIDOMDocument.h"
#include "nsIDOMElement.h"
#include "nsIDOMEventTarget.h"
#include "base/message_loop.h"
#include "base/task.h"

namespace mozilla {
namespace layers {

static int32_t sActivationDelayMs = 100;
static bool sActivationDelayMsSet = false;

ActiveElementManager::ActiveElementManager()
  : mDomUtils(services::GetInDOMUtils()),
    mCanBePan(false),
    mCanBePanSet(false),
    mSetActiveTask(nullptr)
{
  if (!sActivationDelayMsSet) {
    Preferences::AddIntVarCache(&sActivationDelayMs,
                                "ui.touch_activation.delay_ms",
                                sActivationDelayMs);
    sActivationDelayMsSet = true;
  }
}

ActiveElementManager::~ActiveElementManager() {}

void
ActiveElementManager::SetTargetElement(nsIDOMEventTarget* aTarget)
{
  if (mTarget) {
    // Multiple fingers on screen (since HandleTouchEnd clears mTarget).
    ResetActive();
    return;
  }

  mTarget = do_QueryInterface(aTarget);
  TriggerElementActivation();
}

void
ActiveElementManager::HandleTouchStart(bool aCanBePan)
{
  mCanBePan = aCanBePan;
  mCanBePanSet = true;
  TriggerElementActivation();
}

void
ActiveElementManager::TriggerElementActivation()
{
  // Both HandleTouchStart() and SetTargetElement() call this. They can be
  // called in either order. One will set mCanBePanSet, and the other, mTarget.
  // We want to actually trigger the activation once both are set.
  if (!(mTarget && mCanBePanSet)) {
    return;
  }

  // If the touch cannot be a pan, make mTarget :active right away.
  // Otherwise, wait a bit to see if the user will pan or not.
  if (!mCanBePan) {
    SetActive(mTarget);
  } else {
    mSetActiveTask = NewRunnableMethod(
        this, &ActiveElementManager::SetActiveTask, mTarget);
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE, mSetActiveTask, sActivationDelayMs);
  }
}

void
ActiveElementManager::HandlePanStart()
{
  // The user started to pan, so we don't want mTarget to be :active.
  // Make it not :active, and clear any pending task to make it :active.
  CancelTask();
  ResetActive();
}

void
ActiveElementManager::HandleTouchEnd(bool aWasClick)
{
  // If the touch was a click, make mTarget :active right away.
  // nsEventStateManager will reset the active element when processing
  // the mouse-down event generated by the click.
  CancelTask();
  if (aWasClick) {
    SetActive(mTarget);
  }

  // Clear mTarget for next touch.
  mTarget = nullptr;
}

void
ActiveElementManager::SetActive(nsIDOMElement* aTarget)
{
  if (mDomUtils) {
    mDomUtils->SetContentState(aTarget, NS_EVENT_STATE_ACTIVE.GetInternalValue());;
  }
}

void
ActiveElementManager::ResetActive()
{
  // Clear the :active flag from mTarget by setting it on the document root.
  if (mTarget) {
    nsCOMPtr<nsIDOMDocument> doc;
    mTarget->GetOwnerDocument(getter_AddRefs(doc));
    if (doc) {
      nsCOMPtr<nsIDOMElement> root;
      doc->GetDocumentElement(getter_AddRefs(root));
      if (root) {
        SetActive(root);
      }
    }
  }
}

void
ActiveElementManager::SetActiveTask(nsIDOMElement* aTarget)
{
  // This gets called from mSetActiveTask's Run() method. The message loop
  // deletes the task right after running it, so we need to null out
  // mSetActiveTask to make sure we're not left with a dangling pointer.
  mSetActiveTask = nullptr;
  SetActive(aTarget);
}

void
ActiveElementManager::CancelTask()
{
  if (mSetActiveTask) {
    mSetActiveTask->Cancel();
    mSetActiveTask = nullptr;
  }
}

}
}
