#include "MidiView.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;
constexpr float px = 24, py = 116 - top; // the map panel
constexpr int rowH = 31;

void noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
    for (auto* child : c.getChildren())
        noFocus (*child);
}
} // namespace

//==============================================================================
class MidiView::Pill final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Pill (juce::String t, std::function<void()> click)
        : text (std::move (t)),
          onClick (std::move (click))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }
    void setOn (bool o)
    {
        if (o != on)
        {
            on = o;
            repaint();
        }
    }
    void setSize13 (float s) { size = s; }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (on ? colours::amber.withAlpha (0.35f) : isMouseOver() ? colours::line : colours::panel2);
        g.fillRoundedRectangle (r, 7);
        g.setColour (on ? colours::amber : colours::line);
        g.drawRoundedRectangle (r, 7, 1);
        drawText (g,
                  text,
                  r.getCentreX(),
                  r.getCentreY() + size * 0.36f,
                  Fonts::sans (size, on),
                  on ? colours::text : colours::muted,
                  juce::Justification::horizontallyCentred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onClick && getLocalBounds().contains (e.getPosition()))
            onClick();
    }

private:
    juce::String text;
    std::function<void()> onClick;
    bool on = false;
    float size = 12.5f;
};

//==============================================================================
class MidiView::DragValue final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<juce::String (float)> textOf;
    std::function<void (float)> onChange;
    float value = 0, reset = 0;

    DragValue() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours::well);
        g.fillRoundedRectangle (r, 5);
        g.setColour (colours::amber.withAlpha (0.25f));
        g.fillRoundedRectangle (r.withWidth (std::max (2.0f, r.getWidth() * value)), 5);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r, 5, 1);
        drawText (g,
                  textOf ? textOf (value) : juce::String (value, 2),
                  r.getCentreX(),
                  r.getCentreY() + 4,
                  Fonts::mono (10.5f),
                  colours::text,
                  juce::Justification::horizontallyCentred);
    }
    void mouseDown (const juce::MouseEvent&) override { start = value; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        set (start + fine * (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 150.0f);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { set (reset); }
    void set (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (v == value)
            return;
        value = v;
        repaint();
        if (onChange)
            onChange (v);
    }

private:
    float start = 0;
};

//==============================================================================
class MidiView::Row final : public juce::Component
{
public:
    Row (MidiView& v, int i, const MidiMap::Entry& e)
        : view (v),
          index (i),
          entry (e),
          invert (juce::String::fromUTF8 ("⇅"),
                  [this] { view.edit (index, [] (MidiMap::Entry& x) { x.invert = ! x.invert; }, false); }),
          remove (juce::String::fromUTF8 ("×"), [this] { view.removeRow (index); })
    {
        auto& map = view.processor.getMidiMap();
        target.setTextWhenNothingSelected (map.targetName (e.target));
        int id = 1;
        const auto targets = view.allTargets();
        bool headed = false;
        for (int t : targets)
        {
            if (t >= MidiMap::parameterBase && ! headed)
            {
                target.addSectionHeading ("Parameters");
                headed = true;
            }
            target.addItem (map.targetName (t), id++);
        }
        for (size_t k = 0; k < targets.size(); ++k)
            if (targets[k] == e.target)
                target.setSelectedId ((int) k + 1, juce::dontSendNotification);
        target.onChange = [this, targets]
        {
            const int k = target.getSelectedId() - 1;
            if (k < 0 || k >= (int) targets.size())
                return;
            const int t = targets[(size_t) k];
            view.edit (
                index,
                [t] (MidiMap::Entry& x)
                {
                    x.target = t;
                    x.lo = 0.0f;
                    x.hi = 1.0f;
                },
                true);
        };
        target.setTooltip ("What this controller drives: one of the player's dimensions, the slur pedal or any "
                           "parameter.");
        for (auto* d : { &lo, &hi })
        {
            d->textOf = [this] (float x) { return rangeText (x); };
            d->setTooltip ("Drag to set where the controller's 0 (left) and 127 (right) land in the target's range. "
                           "Double-click resets.");
        }
        lo.value = e.lo;
        lo.reset = 0;
        hi.value = e.hi;
        hi.reset = 1;
        lo.onChange = [this] (float x) { view.edit (index, [x] (MidiMap::Entry& y) { y.lo = x; }, false); };
        hi.onChange = [this] (float x) { view.edit (index, [x] (MidiMap::Entry& y) { y.hi = x; }, false); };
        invert.setOn (e.invert);
        invert.setTooltip ("Invert: the controller's top gives the range's start.");
        remove.setTooltip ("Remove this row.");
        for (auto* c : std::initializer_list<juce::Component*> { &target, &lo, &hi, &invert, &remove })
            addAndMakeVisible (c);
    }

    juce::String rangeText (float x) const
    {
        const auto& ps = view.processor.AudioProcessor::getParameters();
        const int i = entry.target - MidiMap::parameterBase;
        if (i >= 0 && i < ps.size())
        {
            const auto t = ps[i]->getText (x, 10), unit = ps[i]->getLabel();
            return unit.isEmpty() || t.endsWith (unit) ? t : t + " " + unit;
        }
        if (entry.target == MidiMap::slurPedal)
            return x >= 0.5f ? "on" : "off";
        return juce::String (juce::roundToInt (x * 100)) + "%";
    }

    void resized() override
    {
        target.setBounds (146, 2, 196, rowH - 5);
        lo.setBounds (348, 4, 62, rowH - 9);
        hi.setBounds (420, 4, 62, rowH - 9);
        invert.setBounds (490, 3, 30, rowH - 7);
        remove.setBounds (getWidth() - 30, 3, 26, rowH - 7);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withTrimmedBottom (3);
        g.setColour (index % 2 == 0 ? colours::panel2 : colours::panel);
        g.fillRoundedRectangle (r, 6);
        const bool learning = view.learnRow == index;
        if (learning)
        {
            g.setColour (colours::amber);
            g.drawRoundedRectangle (r.reduced (0.5f), 6, 1.2f);
        }
        drawText (g,
                  learning ? juce::String::fromUTF8 ("move…") : "CC" + juce::String (entry.cc),
                  6,
                  19,
                  Fonts::mono (12.5f),
                  learning ? colours::amber : colours::gold);
        drawText (g, MidiMap::sourceName (entry.cc), 64, 19, Fonts::sans (11.5f), colours::muted);
        // who has the dimension now (the drawn curves take it over from the player)
        const int t = entry.target;
        const int k = t == 1 ? 0 : t == 26 ? 1 : t == 19 ? 2 : t == 74 ? 3 : -1;
        if (k >= 0)
        {
            const bool taken = view.shownTaken[(size_t) k];
            drawBadge (g, { 538, 14 }, taken ? Mode::guided : Mode::autoMode);
            drawText (g, taken ? "guided" : "auto", 550, 18, Fonts::sans (11), colours::muted);
        }
        else if (t == MidiMap::slurPedal)
            drawText (g,
                      view.shownPedal ? "down" : "up",
                      532,
                      18,
                      Fonts::sans (11),
                      view.shownPedal ? colours::amber : colours::muted);
        else if (t >= MidiMap::parameterBase)
            drawText (g, "param", 532, 18, Fonts::sans (11), colours::dim);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.x < 140)
            view.startLearn (entry.target, view.learnRow == index ? noLearn : index);
    }

    MidiView& view;
    int index;
    MidiMap::Entry entry;
    juce::ComboBox target;
    DragValue lo, hi;
    Pill invert, remove;
};

//==============================================================================
MidiView::MidiView (Processor& p)
    : processor (p),
      curve (&p.getParameters(),
             params::id::velocityCurve.getParamID(),
             "Velocity curve",
             Knob::Style::small,
             [] (float v)
             {
                 return v < 0.97f ? juce::String ("louder ") + juce::String (v, 2)
                     : v > 1.03f  ? juce::String ("softer ") + juce::String (v, 2)
                                  : juce::String ("even");
             }),
      dynamics (&p.getParameters(),
                params::id::dynamics.getParamID(),
                "Dynamics",
                Knob::Style::small,
                [] (float v)
                {
                    return std::abs (v) < 0.5f ? juce::String ("as played")
                                               : (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " %";
                }),
      bend (&p.getParameters(),
            params::id::bendRange.getParamID(),
            "Pitch bend",
            Knob::Style::small,
            [] (float v)
            {
                const int st = juce::roundToInt (v);
                return st == 0 ? juce::String ("off") : juce::String::fromUTF8 ("±") + juce::String (st) + " st";
            }),
      sensitivity (&p.getParameters(),
                   params::id::velocitySensitivity.getParamID(),
                   "Sensitivity",
                   Knob::Style::small,
                   [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      attackWeight (&p.getParameters(),
                    params::id::attackWeight.getParamID(),
                    "Attack weight",
                    Knob::Style::small,
                    [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      octave (&p.getParameters(), params::id::octave.getParamID(), { { "-2" }, { "-1" }, { "0" }, { "+1" }, { "+2" } }),
      behaviour (&p.getParameters(),
                 params::id::keyswitchMode.getParamID(),
                 { { "Latching" }, { "Momentary" }, { "Off" } }),
      mpe (&p.getParameters(), params::id::mpe.getParamID(), { { "Off" }, { "On" } }) // M7
{
    curve.setTooltip ("Velocity to dynamics: below 1 soft playing gets louder sooner, above 1 later.");
    dynamics.setTooltip ("Added to every note's dynamics.");
    bend.setTooltip ("The pitch wheel's full throw, in semitones. It bends the bowed notes; reset all controllers "
                     "(CC121) centres it.");
    octave.setTooltip ("Moves every note by octaves. +1 suits typing keyboards (z = C4 in FL Studio).");
    sensitivity.setTooltip ("How far velocity moves the dynamics. 100 %: pp to ff. Lower: a narrower range around "
                            "velocity 100's dynamic (0 %: every note at it). Above 100 %: wider.");
    attackWeight.setTooltip ("How much velocity moves the attack's bite. 100 %: a harder, quicker catch at ff. 0 %: "
                             "the same attack at every velocity. 200 %: soft notes start gently, loud ones bite.");
    behaviour.setTooltip ("Latching: a keyswitch picks the articulation until the next one. Momentary: only while "
                          "the key is held, then back to what played before. Off: the keys play as ordinary notes "
                          "(silent below the violin). UACC on CC32 picks the articulation too: 1 long, 8 sul tasto, "
                          "10 harmonics, 11-15 tremolo, 20 legato, 40 staccato, 41 martele, 42 spiccato, 43 "
                          "sautille, 44 detache, 50 portato, 56 pizz, 57 left-hand pizz, 58 Bartok, 59 col legno.");
    keyswitchStart = std::make_unique<ValueBox> (
        *p.getParameters().getParameter (params::id::keyswitchStart.getParamID()),
        [] (float v) { return juce::MidiMessage::getMidiNoteName (juce::roundToInt (v), true, true, middleCOctave()); },
        std::vector<float> { 0.0f, 12.0f, 24.0f, 36.0f, 96.0f, 108.0f });
    keyswitchStart->setTooltip ("The first keyswitch key: twelve keys from here pick the nine articulations, then the "
                                "three contact points, whatever the Octave. Drag to move the block, right-click for "
                                "common places, double-click for C1.");
    mpe.setTooltip ("MPE (lower zone, notes on channels 2-16): each note's pitch bend bends it (range: MPE Bend "
                    "Range, 48 semitones), pressure sets the dynamics, CC74 moves the bow towards the bridge.");

    for (int i = 0; i < MidiMap::presetNames().size(); ++i)
        mapPreset.addItem (MidiMap::presetNames()[i], i + 1);
    mapPreset.addItem ("Custom", 99);
    mapPreset.setItemEnabled (99, false);
    mapPreset.setTooltip ("Loads a controller map into the table below. Edit any row afterwards; the map is saved "
                          "with your project.");
    mapPreset.onChange = [this]
    {
        const int id = mapPreset.getSelectedId();
        if (id >= 1 && id <= 3)
            processor.getMidiMap().loadPreset ((MidiMap::Preset) (id - 1));
    };
    learn = std::make_unique<Pill> (juce::String::fromUTF8 ("◉ Learn"), [this] { showLearnMenu(); });
    learn->setTooltip ("Pick what to control, then move a knob, fader or pedal on your controller. Click a row's CC "
                       "to learn that row again.");
    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    for (auto* c : std::initializer_list<juce::Component*> { &curve,
                                                             &dynamics,
                                                             &sensitivity,
                                                             &attackWeight,
                                                             keyswitchStart.get(),
                                                             &bend,
                                                             &octave,
                                                             &behaviour,
                                                             &mpe,
                                                             &mapPreset,
                                                             learn.get(),
                                                             &viewport })
        addAndMakeVisible (c);
    noFocus (*this);
    rebuildRows();
    startTimerHz (15);
}

MidiView::~MidiView()
{
    viewport.setViewedComponent (nullptr, false);
}

std::vector<int> MidiView::allTargets() const
{
    std::vector<int> t;
    for (const auto& p : MidiMap::playerTargets())
        t.push_back (p.code);
    const auto& ps = processor.AudioProcessor::getParameters();
    for (int i = 0; i < ps.size(); ++i)
        t.push_back (MidiMap::parameterBase + i);
    return t;
}

void MidiView::resized()
{
    mapPreset.setBounds ((int) px + 16, (int) py + 56, 340, 34);
    learn->setBounds ((int) px + 492, (int) py + 56, 132, 34);
    viewport.setBounds ((int) px + 12, (int) py + 134, 616, 6 * 0 + 372);
    rowsHolder.setSize (viewport.getWidth() - 10, std::max (1, (int) rows.size()) * rowH);
    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, (int) i * rowH, rowsHolder.getWidth(), rowH);
    // 2.3: two columns of knobs beside the graph (mockup midi.svg: Sensitivity, Attack weight)
    curve.setBounds (1046 - 40, (int) py + 110 - 44, 80, 100);
    dynamics.setBounds (1046 - 40, (int) py + 240 - 44, 80, 100);
    sensitivity.setBounds (1128 - 40, (int) py + 110 - 44, 80, 100);
    attackWeight.setBounds (1128 - 40, (int) py + 240 - 44, 80, 100);
    const int yb = (int) py + 360;
    octave.setLayout (0, 30, 4, 46);
    octave.setBounds (696, yb + 58, 250, 30);
    bend.setBounds (1066, yb + 14, 100, 100);
    keyswitchStart->setBounds (696, yb + 126, 66, 30);
    behaviour.setLayout (0, 30, 4, 80);
    behaviour.setBounds (774, yb + 126, 250, 30);
    mpe.setLayout (0, 30, 4, 54);
    mpe.setBounds (1056, yb + 126, 112, 30);
}

void MidiView::rebuildRows()
{
    shown = processor.getMidiMap().getEntries();
    rows.clear();
    for (size_t i = 0; i < shown.size(); ++i)
    {
        rows.push_back (std::make_unique<Row> (*this, (int) i, shown[i]));
        rowsHolder.addAndMakeVisible (*rows.back());
        noFocus (*rows.back());
    }
    const int preset = processor.getMidiMap().matchingPreset();
    mapPreset.setSelectedId (preset >= 0 ? preset + 1 : 99, juce::dontSendNotification);
    resized();
    repaint();
}

void MidiView::edit (int row, const std::function<void (MidiMap::Entry&)>& fn, bool rebuild)
{
    auto e = processor.getMidiMap().getEntries();
    if (row < 0 || row >= (int) e.size())
        return;
    fn (e[(size_t) row]);
    processor.getMidiMap().setEntries (e);
    if (rebuild)
    {
        // not from inside the control's own callback
        juce::Component::SafePointer<MidiView> safe (this);
        juce::MessageManager::callAsync (
            [safe]
            {
                if (safe != nullptr)
                    safe->rebuildRows();
            });
        return;
    }
    shown = processor.getMidiMap().getEntries();
    if (row < (int) rows.size())
    {
        rows[(size_t) row]->entry = shown[(size_t) row];
        rows[(size_t) row]->invert.setOn (shown[(size_t) row].invert);
        rows[(size_t) row]->repaint();
    }
    const int preset = processor.getMidiMap().matchingPreset();
    mapPreset.setSelectedId (preset >= 0 ? preset + 1 : 99, juce::dontSendNotification);
}

void MidiView::removeRow (int row)
{
    auto e = processor.getMidiMap().getEntries();
    if (row < 0 || row >= (int) e.size())
        return;
    e.erase (e.begin() + row);
    processor.getMidiMap().setEntries (e);
    if (learnRow >= 0)
        learnRow = noLearn;
    juce::Component::SafePointer<MidiView> safe (this);
    juce::MessageManager::callAsync (
        [safe]
        {
            if (safe != nullptr)
                safe->rebuildRows();
        });
}

juce::PopupMenu MidiView::targetMenu (int) const
{
    juce::PopupMenu m, params;
    auto& map = processor.getMidiMap();
    m.addSectionHeader ("The player");
    for (const auto& t : MidiMap::playerTargets())
        m.addItem (t.code + 1, juce::String (t.name) + "   (" + t.range + ")");
    const auto& ps = processor.AudioProcessor::getParameters();
    for (int i = 0; i < ps.size(); ++i)
        params.addItem (MidiMap::parameterBase + i + 1, map.targetName (MidiMap::parameterBase + i));
    m.addSubMenu ("Parameters", params);
    return m;
}

void MidiView::showLearnMenu()
{
    if (learnRow != noLearn)
    {
        learnRow = noLearn; // cancel
        learn->setOn (false);
        learn->setText (juce::String::fromUTF8 ("◉ Learn"));
        repaint();
        return;
    }
    if ((int) processor.getMidiMap().getEntries().size() >= MidiMap::maxEntries)
        return;
    juce::Component::SafePointer<MidiView> safe (this);
    targetMenu (-1).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (learn.get()),
                                   [safe] (int result)
                                   {
                                       if (safe != nullptr && result > 0)
                                           safe->startLearn (result - 1);
                                   });
}

void MidiView::startLearn (int target, int row)
{
    learnRow = row == noLearn ? noLearn : row;
    learnTarget = target;
    learnStart = processor.getMidiMap().incomingCount();
    const bool on = learnRow != noLearn;
    learn->setOn (on);
    learn->setText (on ? juce::String::fromUTF8 ("Move a control…") : juce::String::fromUTF8 ("◉ Learn"));
    for (auto& r : rows)
        r->repaint();
    repaint();
}

void MidiView::timerCallback()
{
    auto& map = processor.getMidiMap();
    // Learn: the first controller that arrives
    const int count = map.incomingCount();
    if (learnRow != noLearn && count != learnStart)
    {
        const int cc = map.lastIncomingCc();
        learnStart = count;
        if (cc < 120 && cc != 0 && cc != 32) // not a channel mode message or a bank select
        {
            auto e = map.getEntries();
            if (learnRow == newRow)
                e.push_back ({ cc, learnTarget });
            else if (learnRow >= 0 && learnRow < (int) e.size())
                e[(size_t) learnRow].cc = cc;
            map.setEntries (e);
            startLearn (0, noLearn);
        }
    }
    if (count != lastIncoming)
    {
        lastIncoming = count;
        const int cc = map.lastIncomingCc();
        const auto name = MidiMap::sourceName (cc);
        incomingText = "Last controller: CC" + juce::String (cc) + (name.isEmpty() ? "" : " " + name) + " = "
            + juce::String (map.lastIncomingValue());
        repaint ((int) px, (int) py + 500, 640, 70);
    }
    if (map.getEntries() != shown)
        rebuildRows();

    // what the player does with the drawn curves now, and the pedal
    const auto& pl = processor.getEngine().getPlayer();
    const std::array<bool, 4> taken { pl.manDyn, pl.ccVib >= 0.0, pl.ccRate >= 0.0, pl.ccContact >= 0.0 };
    const bool pedal = processor.isPedalDown();
    if (taken != shownTaken || pedal != shownPedal)
    {
        shownTaken = taken;
        shownPedal = pedal;
        for (auto& r : rows)
            r->repaint();
    }

    auto& state = processor.getParameters();
    const std::array<float, 4> response {
        state.getRawParameterValue (params::id::velocityCurve.getParamID())->load(),
        state.getRawParameterValue (params::id::dynamics.getParamID())->load(),
        state.getRawParameterValue (params::id::velocitySensitivity.getParamID())->load(),
        state.getRawParameterValue (params::id::keyswitchStart.getParamID())->load()
    };
    if (response != shownResponse)
    {
        shownResponse = response;
        repaint();
    }
    const int v = processor.getTelemetry().velocity.load();
    if (v != shownVelocity && isShowing())
    {
        shownVelocity = v;
        repaint (680, 0, 500, 360);
    }
}

void MidiView::paint (juce::Graphics& g)
{
    const float x = px, y = py;
    drawPanel (g, { x, y, 640, 576 }, "Controller map", "what plays now");
    drawLabel (g, "Map preset", x + 16, y + 50);
    drawLabel (g, "Learn", x + 492, y + 50);

    const float hy = y + 128;
    const float cols[] = { 6, 64, 146, 348, 488, 526 };
    const char* heads[] = { "CC", "Source", "Controls", "Range (0 .. 127)", "Inv", "Mode now" };
    for (int j = 0; j < 6; ++j)
        drawLabel (g, heads[j], x + 12 + cols[j], hy);

    if (learnRow == newRow)
    {
        g.setColour (colours::amber);
        drawText (g,
                  juce::String::fromUTF8 ("Learn: move a control for ")
                      + processor.getMidiMap().targetName (learnTarget) + juce::String::fromUTF8 ("…"),
                  x + 16,
                  y + 106,
                  Fonts::sans (12, true),
                  colours::amber);
    }
    else
        drawText (g,
                  juce::String (shown.size()) + " of " + juce::String (MidiMap::maxEntries)
                      + " rows. Several rows may share a CC. Velocity always sets the note's dynamics.",
                  x + 16,
                  y + 106,
                  Fonts::sans (11),
                  colours::dim);

    drawText (g,
              incomingText.isEmpty() ? juce::String ("No controller received yet.") : incomingText,
              x + 16,
              y + 530,
              Fonts::mono (11),
              colours::muted);
    drawText (g,
              juce::String::fromUTF8 ("Pitch wheel: bowed notes ± Pitch bend · CC121 gives the curves back to the "
                                      "player"),
              x + 16,
              y + 552,
              Fonts::sans (11),
              colours::dim);

    paintVelocity (g, { 680, y, 496, 348 });

    const float yb = y + 360;
    drawPanel (g, { 680, yb, 496, 216 }, "Octave, bend, keyswitches and MPE");
    drawLabel (g, "Octave", 696, yb + 50);
    drawLabel (g, "Start key", 696, yb + 118);
    drawLabel (g, "Behaviour", 774, yb + 118);
    drawLabel (g, "MPE", 1056, yb + 118);
    auto name = [] (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, middleCOctave()); };
    const int ks = juce::roundToInt (
        processor.getParameters().getRawParameterValue (params::id::keyswitchStart.getParamID())->load());
    drawText (g,
              juce::String::fromUTF8 ("Keyswitches (") + name (ks) + juce::String::fromUTF8 ("–")
                  + name (ks + params::keyswitchCount - 1)
                  + juce::String::fromUTF8 (") and UACC (CC32) pick the articulation; MPE: pressure"),
              696,
              yb + 182,
              Fonts::sans (11),
              colours::dim);
    drawText (g,
              juce::String::fromUTF8 ("→ dynamics, slide → contact. +1 octave: z on a typing keyboard plays C4."),
              696,
              yb + 199,
              Fonts::sans (11),
              colours::dim);
}

void MidiView::paintVelocity (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawPanel (g, r, "Velocity", "full range");
    const float gx = r.getX() + 62, gy = r.getY() + 56, gw = 262, gh = 226;
    g.setColour (colours::well);
    g.fillRoundedRectangle (gx, gy, gw, gh, 4);
    static const char* dyn[] = { "pp", "p", "mp", "mf", "f", "ff" };
    for (int i = 0; i < 6; ++i)
    {
        const float yy = gy + gh - (i + 0.5f) * gh / 6;
        g.setColour (colours::line);
        g.drawLine (gx, yy, gx + gw, yy, 0.6f);
        drawText (g, dyn[i], gx - 12, yy + 5, Fonts::serif (14), colours::muted, juce::Justification::right);
    }
    for (int v : { 1, 32, 64, 100, 127 })
    {
        const float xx = gx + gw * v / 127.0f;
        g.setColour (colours::muted);
        g.drawLine (xx, gy + gh, xx, gy + gh + 5);
        drawText (g,
                  juce::String (v),
                  xx,
                  gy + gh + 18,
                  Fonts::mono (10),
                  colours::muted,
                  juce::Justification::horizontallyCentred);
    }
    drawText (g,
              "velocity",
              gx + gw / 2,
              gy + gh + 36,
              Fonts::sans (10),
              colours::dim,
              juce::Justification::horizontallyCentred);

    const auto& pp = processor.getEngine().getPlayer().pp;
    auto& state = processor.getParameters();
    const double c = state.getRawParameterValue (params::id::velocityCurve.getParamID())->load();
    const double bias = state.getRawParameterValue (params::id::dynamics.getParamID())->load() / 100.0;
    const double sens = state.getRawParameterValue (params::id::velocitySensitivity.getParamID())->load() / 100.0;
    auto dynOf = [&pp] (double v, double curve, double b, double sn)
    {
        // as o2::Player::dynFromVel (2.3: Sensitivity around velocity velSensRef's dynamic)
        const double x = std::clamp ((v - pp.velLo) / (pp.velHi - pp.velLo), 0.0, 1.0);
        const double r = std::pow (std::clamp ((pp.velSensRef - pp.velLo) / (pp.velHi - pp.velLo), 0.0, 1.0), curve);
        return std::clamp (r + sn * (std::pow (x, curve) - r) + b, 0.0, 1.0);
    };
    auto pointOf = [&] (double v, double curve, double b, double sn = 1.0)
    { return juce::Point<float> (gx + gw * (float) v / 127.0f, gy + gh - (float) dynOf (v, curve, b, sn) * gh); };
    for (int pass = 0; pass < 2; ++pass)
    {
        juce::Path p;
        for (int v = 1; v <= 127; ++v)
        {
            const auto pt = pass == 0 ? pointOf (v, 1.0, 0.0) : pointOf (v, c, bias, sens);
            v == 1 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        if (pass == 0)
        {
            juce::Path dashed;
            const float dashes[] = { 4, 4 };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, p, dashes, 2);
            g.setColour (colours::muted);
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (colours::amber);
            g.strokePath (p, juce::PathStrokeType (2.5f));
        }
    }
    for (int v : { 64, 100, 127 })
    {
        const auto pt = pointOf (v, c, bias, sens);
        g.setColour (colours::gold);
        g.fillEllipse (pt.x - 4.5f, pt.y - 4.5f, 9, 9);
        drawText (g,
                  juce::String (v) + " = " + dynamicName ((float) dynOf (v, c, bias, sens)),
                  pt.x - 8,
                  pt.y - 10,
                  Fonts::sans (10.5f),
                  colours::gold,
                  juce::Justification::right);
    }
    if (shownVelocity > 0)
    {
        const auto pt = pointOf (shownVelocity, c, bias, sens);
        g.setColour (colours::steel);
        g.drawEllipse (pt.x - 7, pt.y - 7, 14, 14, 2);
        drawText (g,
                  "last note " + juce::String (shownVelocity),
                  gx + gw - 8,
                  gy + 18,
                  Fonts::sans (10.5f),
                  colours::steel,
                  juce::Justification::right);
    }
    drawText (g,
              "dashed = even curve",
              gx + gw,
              gy + gh + 36,
              Fonts::sans (10.5f),
              colours::muted,
              juce::Justification::right);
}
} // namespace octavio2::ui
