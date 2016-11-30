#pragma once

/*
 *      Copyright (C) 2012-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GUIWindowPVRCommon.h"
#include "epg/EpgSearchFilter.h"

namespace PVR
{
  class CGUIWindowPVR;

  class CGUIWindowPVRSearch : public CGUIWindowPVRCommon
  {
    friend class CGUIWindowPVR;
    friend class CGUIWindowPVRCommon;

  public:
    CGUIWindowPVRSearch(CGUIWindowPVR *parent);
    virtual ~CGUIWindowPVRSearch(void) {};

    void GetContextButtons(int itemNumber, CContextButtons &buttons) const;
    bool OnContextButton(int itemNumber, CONTEXT_BUTTON button);
    void UpdateData(bool bUpdateSelectedFile = true);
    //TBIRD 1.3.4 MODS - START
    void ShowSearchResults();
    //TBIRD 1.3.4 MODS - FINISH
    //TBIRD 1.3.5 MODS - START
    void RecordAll();
    //TBIRD 1.3.5 MODS - FINISH

  private:

    bool OnClickButton(CGUIMessage &message);
    bool OnClickList(CGUIMessage &message);

    bool OnContextButtonClear(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonInfo(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonStartRecord(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonStopRecord(CFileItem *item, CONTEXT_BUTTON button);
    //TBIRD-STAN 1.3.5 MODS - START
    //TBIRD 1.3.4 MODS - START
    /*bool OnContextButtonUser1(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonUser2(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonUser3(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonUser4(CFileItem *item, CONTEXT_BUTTON button);
    bool OnContextButtonUser5(CFileItem *item, CONTEXT_BUTTON button);*/
    //TBIRD 1.3.4 MODS - FINISH
    //TBIRD-STAN 1.3.5 MODS - FINISH

    bool ActionShowSearch(CFileItem *item);
//BAD Jin 22012015
    //void ShowSearchResults();
//BAD Jin

    bool               m_bSearchStarted;
    bool               m_bSearchConfirmed;
    EPG::EpgSearchFilter m_searchfilter;
  };
}
