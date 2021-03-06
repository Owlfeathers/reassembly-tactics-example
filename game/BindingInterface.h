
#ifndef BINDING_INT_H
#define BINDING_INT_H

#include "Game.h"
#include "Player.h"
#include "Keys.h"

static const uint kBIColorAct = ALPHA_OPAQUE|COLOR_C;
static const uint kBIColorDef = 0x80e0a040;
static const uint kBIColorHi = ALPHA_OPAQUE|0xf0c080;

extern float2 kConstructorViewBounds;

struct BindingInterface final : public ITabInterface {

    Button          m_ship;
    GLRenderTexture m_shipTex;
    View            m_shipView;
    vector<Button>  m_groupButtons;
    vector<Button>  m_blockButtons;
    vector<Block *> m_blocks;
    BlockCluster*   m_cluster = NULL;
    vector<Button*> m_selected;
    Button          m_reset;
    BContext<ECommandFlags> m_aibehavior;
    FlyCam          m_cam;

    float3          m_savePos;
    bool            m_cameraInited = false;
    bool            m_bindingChanged = false;
    bool*           m_pBindingChanged = NULL;
    bool            m_swappingOut = false;
    std::mutex      m_mutex;

    mutable string  m_tooltip;
    mutable int     m_tooltipIdent = -1;

    string getTooltip(int idx) const
    {
        if (m_tooltipIdent == idx)
            return m_tooltip;
        
        if (idx < 0) {
            idx = -idx -1;
            string str = KeyBindings::getFireDescription(idx);
            if (idx != KeyBindings::FG_DEFENSE) {
                str += "\n";
                str += (m_cluster->data.wgroup[idx] == WF_RIPPLE_FIRE) ?
                       _("Ripple Fire alternates weapons to maximize rate of fire") :
                       _("Fire all weapons in group simultaneously");
            }
            m_tooltip = str;
        }
        else
        {
            lstring name = m_blocks[idx]->sb.name;
            m_tooltip = str_format(_("Double click to select all %ss"), name.c_str());
        }
        m_tooltipIdent = idx;
        return m_tooltip;
    }
    
    string getGroupSubtext(int idx) const
    {
        if (idx <= m_cluster->data.wgroup.size())
            return (m_cluster->data.wgroup[idx] == WF_RIPPLE_FIRE) ? _("Ripple Fire") : _("Fire All");
        else
            return "";
    }

    BindingInterface()
    {
        m_cam.m_viewRadiusMin = kConstructorViewBounds.x;
        m_cam.m_viewRadiusMax = kConstructorViewBounds.y;
        
        for (int i=0; i<KeyBindings::FG_COUNT; i++)
        {
            Button bu;
            if (i < KeyBindings::FireKeyCount) {
                bu.text = KeyBindings::instance().getFireKey(i).getKeyStr();
            } else {
                bu.text = KeyBindings::instance().getFireName(i);
            }
            
            bu.ident = -(i+1);
            bu.style = S_BOX|S_FIXED;
            m_groupButtons.push_back(bu);
        }

        m_ship.defaultBGColor = ALPHAF(0.5f);
        m_ship.style = S_BOX|S_FIXED;
        
        m_reset.text = _("Reset Defaults");
        m_reset.textSize = 18;
        m_reset.style = S_BOX;
        m_reset.textColor = kGUITextLow;

        m_aibehavior.title = _("AI Behavior");
        m_aibehavior.textSize = 18;
        m_aibehavior.style = S_BOX;
        m_aibehavior.pushItem(_("Dynamic"), 0);
        m_aibehavior.pushItem(_("Rush"), ECommandFlags::ALWAYS_RUSH);
        m_aibehavior.pushItem(_("Maneuver"), ECommandFlags::ALWAYS_MANEUVER);
        m_aibehavior.pushItem(_("Kite"), ECommandFlags::ALWAYS_KITE);
    }

    void onSwapIn()
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_swappingOut = false;
        m_bindingChanged = false;
        m_tooltipIdent = -1;
        m_cluster = or_(m_cluster,
                        globals.save->getAppliedBlueprint(),
                        globals.getPlayer() ? globals.getPlayer()->cluster : NULL);

        globals.save->doProgress(EProgress::USED_BINDINGS);

        if (!m_cluster)
            return;
        m_savePos = f3(m_cluster->getPos(), m_cluster->getAngle());
        m_shipView.setAngle(-M_PIf / 3.f);
        m_cluster->setAngle(0.f);
        m_cluster->setPos(float2(0.f));
        m_cluster->onInstantiate();
        if (m_cluster->command)
            m_aibehavior.select(m_cluster->command->sb.command->flags&AIBEHAVIOR_COMMAND_FLAGS);

        m_cameraInited = false;
        
        m_selected.clear();
        m_blocks.clear();
        m_blockButtons.clear();
        
        for (int i=0; i<=KeyBindings::FG_AUTO; i++)
        {
            m_groupButtons[i].subtext = getGroupSubtext(i);
        }

        int i=0;
        foreach (Block *bl, m_cluster->blocks)
        {
            if (bl->sb.features&(Block::LAUNCHER|Block::CANNON|Block::LASER))
            {
                m_blocks.push_back(bl);
                m_blockButtons.push_back(Button("", "", i));
                Button& bu = m_blockButtons.back();
                bu.defaultBGColor = 0;
                bu.pressedBGColor = SetAlphaAXXX(0xf0404040, 0.25f);
                i++;
            }
        }

    }
    
    bool HandleEvent(const Event* event)
    {
        if (!m_cluster)
            return false;
        std::lock_guard<std::mutex> l(m_mutex);

        bool handled = false;

        bool isActivate = false;
        bool isPress = false;
        foreach (Button& bu, m_groupButtons) {
            if (ButtonHandleEvent(bu, event, &isActivate, &isPress))
            {
                const uint idx = -bu.ident - 1;
                if (!m_selected.empty())
                {
                    if (event->type == Event::MOUSE_UP)
                    {
                        foreach (Button *bb, m_selected)
                        {
                            uchar &bindingId = m_blocks[bb->ident]->sb.bindingId;
                            bindingId = -bu.ident;
                            m_bindingChanged = (bindingId != -bu.ident);
                        }
                        m_selected.clear();
                    }
                }
                else if (isActivate && idx <= KeyBindings::FG_AUTO)
                {
                    m_cluster->data.wgroup[idx] = ((m_cluster->data.wgroup[idx] == WF_RIPPLE_FIRE)
                                                   ? WF_FIRE_ALL : WF_RIPPLE_FIRE);
                    bu.subtext = getGroupSubtext(idx);
                    m_bindingChanged = true;
                }
                handled = true;
            }
        }

        int idx = 0;
        foreach (Button& bu, m_blockButtons) {
            if (ButtonHandleEvent(bu, event, NULL, &isPress))
            {
                if (isPress)
                {
                    m_selected.clear();
                    if (globals.isDoubleClick(event->key))
                    {
                        for (int i=0; i<m_blockButtons.size(); i++)
                        {
                            if (m_blocks[i]->sb.ident == m_blocks[idx]->sb.ident)
                            {
                                m_selected.push_back(&m_blockButtons[i]);
                            }
                        }
                    }
                    else
                    {
                        m_selected.push_back(&bu);
                    }
                }
                handled = true;
            }
            idx++;
        }

        if (handled)
            return true;

        if ((event->type == Event::MOUSE_DOWN || event->isEscape()) && m_selected.size())
        {
            m_selected.clear();
            return true;
        }

        if (ButtonHandleEvent(m_reset, event, &isActivate))
        {
            if (isActivate)
            {
                setupDefaultWeaponBindings(m_cluster, true);
                m_bindingChanged = true;
            }
            return true;
        }

        if (m_aibehavior.HandleEventMenu(event, &isActivate))
        {
            if (isActivate)
            {
                const ECommandFlags flag = m_aibehavior.getSelection();
                foreach (Block *bl, m_cluster->blocks)
                {
                    if (!bl->isCommand() || !bl->sb.command)
                        continue;
                    bl->sb.command->flags &= ~AIBEHAVIOR_COMMAND_FLAGS;
                    bl->sb.command->flags |= flag;
                    m_bindingChanged = true;
                }
            }
            return true;
        }

        if (event->type == Event::SCROLL_WHEEL)
        {
            m_cam.adjustZoom(1.f - (event->vel.y * kCameraWheelZSpeed));
        }

        return false;
    }

    static void PushLine(float2 p0, float2 r0, float2 p1, float2 r1)
    {
        float2 vec = normalize(p1 - p0);
        vec.x = (vec.x < -0.5f) ? -1.f : (vec.x > 0.5f) ? 1.f : 0.f;
        vec.y = (vec.y < -0.5f) ? -1.f : (vec.y > 0.5f) ? 1.f : 0.f;
        float2 v1 = p1 - vec * r1;
        intersectSegmentSegment(&v1, p0, p1, p1 - r1, p1 + flipX(r1));
        theDMesh().line.PushLine(p0 + vec * r0, v1);
    }

    virtual void renderTab(const View &view)
    {
        std::lock_guard<std::mutex> l(m_mutex);
        const float2 center = view.center;
        const float2 size = view.size;

        // camera control
        if (m_cameraInited && m_cluster)
        {
            m_cam.panZoom(m_shipView);
            const float wrad = min_dim(m_shipView.getWorldSize() / 2.f);
            const float brad = 0.75f * m_cluster->getBRadius();
            if (!intersectCircleCircle(m_cluster->getPos(), brad, m_shipView.position, wrad))
            {
                m_shipView.position = normalize(m_shipView.position) * (brad + wrad);
            }
            m_shipView.rotate(-KeyBindings::instance().getRotate() * kCameraRotationSpeed * (float)globals.frameTime);
        }

        ShaderState s1 = view.getScreenShaderState();

        theDMesh().start();
        m_ship.position = center - justX(size/2.f) + float2(3.f * size.x / 10.f, 0.f);
        m_ship.size = float2(3.f * size.x / 5.f, size.y - kButtonPad.y);
        m_ship.alpha = view.alpha;
        m_ship.renderButton(theDMesh());

        const float2 start = center + size/2.f - float2(size.x / 5.f, 0.f);
        float2 pos = start;
        pos.y -= GLText::Put(s1, start, GLText::DOWN_CENTERED, MultAlphaAXXX(kGUIText, view.alpha),
                             36, _("Weapon Bindings")).y;
        const float bheight = size.y - (start.y - pos.y);

        string tooltip;
        
        pos.y -= 0.5f * bheight / m_groupButtons.size();
        foreach (Button& bu, m_groupButtons)
        {
            bu.size.y = bheight / (m_groupButtons.size() + 2);
            bu.size.x = size.x / 4.f - kButtonPad.x;
            bu.defaultLineColor = kBIColorDef;
            bu.position = pos;
            bu.alpha = view.alpha;
            pos.y -= bheight / m_groupButtons.size();

            if (bu.hovered)
                tooltip = getTooltip(bu.ident);
        }
        
        for (int i=0; i<m_blockButtons.size(); i++)
        {
            Button &bb = m_blockButtons[i];
            Block  *bl = m_blocks[i];

            bb.position = m_shipView.toScreen(bl->getAbsolutePos()) + m_ship.position - m_ship.size / 2.f;
            bb.size     = float2(m_shipView.toScreenSize(2.f * bl->spec->minradius) - kButtonPad.x);
            bb.alpha    = view.introAnim;
            bb.visible  = intersectPointRectangle(bb.position, m_ship.position, m_ship.size/2.f - bb.size/2.f);

            const bool isSelected = vec_contains(m_selected, &bb);
            uint       defLColor  = kBIColorDef;
            
            if (bb.visible && 0 < bl->sb.bindingId && bl->sb.bindingId <= m_groupButtons.size())
            {
                Button &group = m_groupButtons[bl->sb.bindingId-1];
                const uint color = (bb.hovered || group.hovered) ? kBIColorHi : kBIColorDef;
                if (bb.hovered || group.hovered)
                    group.defaultLineColor = color;
                defLColor = color;
                
                theDMesh().line.color32(color, view.introAnim);
                PushLine(bb.position, bb.size / 2.f, group.position, group.size / 2.f);
            }

            if (bb.hovered)
                tooltip = getTooltip(bb.ident);

            bb.defaultLineColor = isSelected ? kBIColorAct : defLColor;
            bb.hoveredLineColor = isSelected ? kBIColorAct : kBIColorHi;
        }

        foreach (Button *bu, m_selected)
        {
            theDMesh().line.color32(kBIColorAct, view.alpha);
            PushLine(bu->position, bu->size / 2.f, KeyState::instance().cursorPosScreen, float2());
        }

        // draw ship cluster (under ship button)
        if (m_cluster)
        {
            const float pointSize = globals.windowSizePixels.x / globals.windowSizePoints.x;
            m_shipView.sizePoints = round(m_ship.size);
            m_shipView.sizePixels = round(m_shipView.sizePoints * pointSize);

            if (!m_cameraInited)
            {
                const AABBox bbox = m_cluster->bbox;
                m_shipView.scale = 1.f / min_dim((m_shipView.sizePoints - kButtonPad) / (2.f *  bbox.getRadius()));
                m_shipView.position = m_cluster->getPos() + rotate(bbox.getCenter(), m_shipView.rot);
                m_cam.m_targetWorldRadius = m_shipView.getWorldRadius();
                m_cameraInited = true;
            }

            if (!m_swappingOut)
            {
                m_shipTex.BindFramebuffer(m_shipView.sizePixels);
                {
                    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                    const ShaderState ws = m_shipView.getWorldShaderState();
                    m_cluster->renderInZone(ws, m_shipView);
                }
                m_shipTex.UnbindFramebuffer();
            }

            ShaderState ts = s1;
            ts.color(COLOR_WHITE, view.alpha);
            GLScope s(GL_DEPTH_TEST, false);
            ShaderTexture::instance().DrawRect(ts, m_shipTex, m_ship.position, m_ship.size/2.f);
        }

        // draw ship button
        theDMesh().tri.Draw(s1, ShaderColor::instance());
        theDMesh().tri.clear();

        foreach (Button &bu, m_blockButtons)
        {
            bu.renderButton(theDMesh(), false);
        }

        foreach (Button& bu, m_groupButtons)
        {
            bu.hoveredLineColor = !m_selected.empty() ? kBIColorAct : kBIColorHi;
            bu.renderButton(theDMesh(), false);
        }

        // draw lines connecting things
        theDMesh().line.Draw(s1, ShaderColor::instance());
        theDMesh().line.clear();

        // draw buttons on top of lines
        theDMesh().tri.Draw(s1, ShaderColor::instance());
        theDMesh().tri.clear();

        foreach (Button& bu, m_groupButtons) {
            bu.renderContents(s1);
        }
        foreach (Button& bu, m_blockButtons) {
            bu.renderContents(s1);
        }

        theDMesh().Draw(s1, ShaderColor::instance(), ShaderColor::instance());
        theDMesh().finish();

        s1.translateZ(0.5f);
        
        const float2 tpos = m_ship.position - m_ship.size / 2.f + 2.f * kButtonPad;
        if (tooltip.size()) {
            GLText::Put(s1, tpos, GLText::LEFT, kBIColorAct, 14.f, tooltip);
        } else {
            m_reset.position = tpos + m_reset.size / 2.f;
            m_reset.alpha = view.alpha;
            m_reset.render(s1);
        }

        m_aibehavior.position = m_ship.position + flipX(m_ship.size/2.f) +
                                flipY(m_aibehavior.size / 2.f + 4.f * kButtonPad);
        m_aibehavior.alpha = view.alpha;
        m_aibehavior.render(s1);
    }

    virtual void onSwapOut()
    {
        m_swappingOut = true;
        if (!m_cluster)
            return;
        if (m_bindingChanged || kTestAlwaysSaveBlueprints)
        {
            if (m_pBindingChanged)
                *m_pBindingChanged = true;
            else
                globals.save->writeBlueprintsToFile();
        }
        if (m_cluster->zone)
        {
            // special case for sandbox where ship may not have blueprint
            m_cluster->setPos(f2(m_savePos));
            m_cluster->setAngle(m_savePos.z);
            m_cluster->applyBlueprintBindings(m_cluster);
        }
    }

    virtual void onStep()
    {
    }
    
};


#endif // BINDING_INT_H
