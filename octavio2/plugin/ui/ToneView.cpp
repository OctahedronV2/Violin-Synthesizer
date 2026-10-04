#include "ToneView.h"
#include "Instruments.h"

namespace octavio2::ui
{
namespace
{
constexpr float top = 104;

juce::String db (float v)
{
    return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
}

std::vector<Choices::Item> previewItems (std::initializer_list<const char*> names)
{
    std::vector<Choices::Item> items;
    for (auto* n : names)
        items.push_back ({ juce::String::fromUTF8 (n) });
    return items;
}
} // namespace

ToneView::ToneView (Processor& p)
    : processor (p),
      body (&p.getParameters(),
            params::id::violin.getParamID(),
            { { "Stoppani", "warm, even" },
              { "Klimke", "bright, focused" },
              { "Levaggi", "dark, round" },
              { "Iowa", "from recordings" } }),
      strings (&p.getParameters(), params::id::strings.getParamID(), previewItems ({ "Synthetic", "Gut", "Steel" })),
      rosin (&p.getParameters(),
             params::id::rosin.getParamID(),
             previewItems ({ "Light", "Standard", "Dark", "Baroque" })),
      bow (&p.getParameters(), params::id::bow.getParamID(), previewItems ({ "Modern", "Baroque" })),
      hiss (&p.getParameters(), params::id::hiss.getParamID(), previewItems ({ "1x", "3x", "6x" })),
      mute (&p.getParameters(), params::id::mute.getParamID(), previewItems ({ "Off", "Sordino", "Practice" })),
      quality (nullptr, {}, previewItems ({ "High (96 kHz strings)", "Eco" })),
      rooms (&p.getParameters(),
             params::id::room.getParamID(),
             previewItems ({ "Close mics",
                             "Arvedi near",
                             "Arvedi far",
                             "Brahmssaal",
                             "Church",
                             "Maida Vale 4",
                             "Maida Vale 5",
                             "WDR control",
                             "WDR studio" })),
      bridge (&p.getParameters(),
              params::id::bridge.getParamID(),
              "Bridge",
              Knob::Style::small,
              [] (float v) { return juce::String (v / 1000.0f, 2) + " kHz"; }),
      sympathetic (&p.getParameters(),
                   params::id::sympathetic.getParamID(),
                   "Sympathetic",
                   Knob::Style::small,
                   [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      wolf (&p.getParameters(),
            params::id::wolf.getParamID(),
            "Wolf",
            Knob::Style::small,
            [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      hold (&p.getParameters(),
            params::id::hold.getParamID(),
            "Hold",
            Knob::Style::small,
            [] (float v) { return v < 0.5f ? juce::String ("free") : juce::String (juce::roundToInt (v)) + " %"; }),
      brilliance (&p.getParameters(), params::id::brightness.getParamID(), "Brilliance", Knob::Style::small, db),
      imperfection (&p.getParameters(),
                    params::id::imperfection.getParamID(),
                    "Imperfection",
                    Knob::Style::small,
                    [] (float v)
                    { return v < 0.5f ? juce::String ("none") : juce::String (juce::roundToInt (v)) + " %"; }),
      reverb (&p.getParameters(), params::id::reverb.getParamID(), "Reverb", Knob::Style::small, db),
      volume (&p.getParameters(), params::id::volume.getParamID(), "Volume", Knob::Style::small, db),
      width (&p.getParameters(),
             params::id::width.getParamID(),
             "Width",
             Knob::Style::small,
             [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      movement (&p.getParameters(),
                params::id::movement.getParamID(),
                "Movement",
                Knob::Style::small,
                [] (float v)
                { return v < 0.5f ? juce::String ("still") : juce::String (juce::roundToInt (v)) + " %"; }),
      distance (&p.getParameters(),
                params::id::distance.getParamID(),
                "Distance",
                Knob::Style::small,
                [] (float v) { return juce::String (v, 1) + " m"; }),
      instruments (p.getParameters())
{
    body.setTooltip ("Measured violin bodies, made full band. Iowa is estimated from recordings.");
    strings.setTooltip ("Synthetic: modern perlon-core strings. Gut: lower tension and more internal loss, "
                        "darker and quicker to fade. Steel: a fiddler's steel core, bright and ringing.");
    rosin.setTooltip ("How the rosin grips: light is hard and smooth, dark is soft and sticky (more bite and "
                      "grit), baroque the softest. It changes the friction law, not a filter.");
    bow.setTooltip ("Baroque: a shorter, lighter bow with a light tip, played with lift-off strokes that let "
                    "each note breathe and ring.");
    hiss.setTooltip ("The hiss of the hair sliding on the string. 1x is a real violin's balance as heard in the "
                     "room; 3x and 6x bring it forward.");
    instruments.setTooltip ("One click sets the parts: Baroque violin = gut strings, baroque rosin and bow (set "
                            "A4 = 415 Hz on the Tuning row). Modern violin sets them back. Any part can still be "
                            "changed.");
    mute.setTooltip (
        "A mute adds mass to the bridge: sordino veils the tone, the practice mute is for quiet practice.");
    quality.setTooltip ("Eco arrives with the optimisation milestone (M8).");
    rooms.setTooltip ("Measured rooms (impulse responses, credits in the About box). Close mics: the violin alone.");
    bridge.setTooltip ("The bridge's rocking resonance: lower is darker, higher is brighter.");
    sympathetic.setTooltip ("How freely the open strings you are not playing ring along with the notes.");
    wolf.setTooltip ("A wolf at the body's strongest resonance (near C5): those notes go rough and unsteady.");
    hold.setTooltip ("The chin and hand on the violin damp its low resonances. 0: hanging free.");
    imperfection.setTooltip (
        "A less perfect player, as Octavio 1's: notes land a little out of tune and are corrected "
        "by ear, the bow is uneven from stroke to stroke, the timing loosens. 0: the clean player.");
    width.setTooltip ("0: both speakers hear one direction. 100 %: the two microphones as placed.");
    movement.setTooltip ("The player's slow sway, which turns the violin between directions.");
    distance.setTooltip ("How far the microphones are: the room's share, its delay and the air's treble loss.");
    micAttachment
        = std::make_unique<juce::ParameterAttachment> (*p.getParameters().getParameter (params::id::mic.getParamID()),
                                                       [this] (float v)
                                                       {
                                                           mic = juce::roundToInt (v);
                                                           repaint();
                                                       });
    micAttachment->sendInitialUpdate();
    // M7 tuning row
    a4 = std::make_unique<ValueBox> (
        *p.getParameters().getParameter (params::id::a4.getParamID()),
        [] (float v) { return "A4 = " + juce::String (v, std::abs (v - std::round (v)) < 0.05f ? 0 : 1) + " Hz"; },
        std::vector<float> { 415.0f, 430.0f, 432.0f, 440.0f, 442.0f, 443.0f, 466.0f });
    a4->setTooltip ("The A4 reference: 440 modern, 442-443 many orchestras, 415 baroque, 466 high baroque. Drag "
                    "to change (shift: finely), right-click for common values. The open strings retune with it.");
    for (int i = 0; i < params::intonationNames().size(); ++i)
        intonation.addItem (params::intonationNames()[i], i + 1);
    for (int i = 0; i < params::keyNames().size(); ++i)
        key.addItem ("Key: " + params::keyNames()[i], i + 1);
    intonationAttachment
        = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.getParameters(),
                                                                                    params::id::intonation.getParamID(),
                                                                                    intonation);
    keyAttachment
        = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.getParameters(),
                                                                                    params::id::tuningKey.getParamID(),
                                                                                    key);
    intonation.setTooltip ("Expressive: the player's own (equal temperament corrected by ear; the Romantic, Baroque "
                           "and other styles lean Pythagorean). Equal: equal temperament. Just and Pythagorean: "
                           "pure intervals from the Key, open strings in pure fifths. Scala file: a .scl scale (with "
                           "an optional .kbm map). MTS-ESP: the tuning of a running MTS-ESP master.");
    key.setTooltip ("The tonic for Just and Pythagorean (and the Expressive leaning of the styles). The A stays at "
                    "the A4 reference.");
    scalaButton.setTooltip ("Load a Scala scale (.scl), and optionally its keyboard map (.kbm, select both). It is "
                            "saved with your project and plays when Intonation is Scala file.");
    scalaButton.onClick = [this] { chooseScala(); };
    addAndMakeVisible (*a4);
    addAndMakeVisible (intonation);
    addAndMakeVisible (key);
    addAndMakeVisible (scalaButton);
    brilliance.setTooltip ("A high shelf at 1.5 kHz on the bridge force (the strings' sparkle).");
    reverb.setTooltip ("The room's level against the direct sound.");
    for (auto* c : { &body, &strings, &rosin, &bow, &hiss, &mute, &quality, &rooms })
        addAndMakeVisible (c);
    addAndMakeVisible (instruments);
    for (auto* k : { &bridge,
                     &sympathetic,
                     &wolf,
                     &hold,
                     &brilliance,
                     &imperfection,
                     &reverb,
                     &volume,
                     &width,
                     &movement,
                     &distance })
        addAndMakeVisible (k);
    startTimerHz (30);
}

void ToneView::resized()
{
    const int y = juce::roundToInt (116 - top);
    // instrument
    body.setLayout (1, 30, 6);
    body.setBounds (40, y + 60, 328, 4 * 39);
    strings.setLayout (0, 30, 4, 104);
    strings.setBounds (40, y + 290, 328, 30);
    rosin.setLayout (0, 30, 4, 77);
    rosin.setBounds (40, y + 350, 328, 30);
    bow.setLayout (0, 30, 4, 98);
    bow.setBounds (40, y + 410, 200, 30);
    hiss.setLayout (0, 30, 4, 38);
    hiss.setBounds (252, y + 410, 122, 30);
    instruments.setBounds (40, y + 232, 328, 30);
    // tuning (M7)
    a4->setBounds (40, y + 470, 160, 34);
    intonation.setBounds (208, y + 470, 160, 34);
    key.setBounds (40, y + 512, 104, 30);
    scalaButton.setBounds (152, y + 512, 112, 30);
    // bridge and resonance
    int i = 0;
    for (auto* k : { &bridge, &sympathetic, &wolf, &hold, &brilliance, &imperfection })
    {
        const int cx = 396 + 70 + (i % 3) * 110, cy = y + 100 + (i / 3) * 120;
        k->setBounds (cx - 50, cy - 44, 100, 100);
        ++i;
    }
    mute.setLayout (0, 30, 4, 104);
    mute.setBounds (412, y + 318, 328, 30);
    quality.setLayout (0, 30, 4, 160);
    quality.setBounds (412, y + 380, 328, 30);
    // room
    rooms.setLayout (3, 32, 8);
    rooms.setBounds (784, y + 314, 376, 3 * 44);
    i = 0;
    for (auto* k : { &distance, &reverb, &width, &movement, &volume })
    {
        const int cx = 768 + 52 + i * 76, cy = y + 504;
        k->setBounds (cx - 38, cy - 44, 76, 100);
        ++i;
    }
}

void ToneView::timerCallback()
{
    if (isShowing())
        repaint();
}

void ToneView::paint (juce::Graphics& g)
{
    const float y = 116 - top;
    // instrument
    drawPanel (g, { 24, y, 360, 576 }, "Instrument");
    drawLabel (g, "Body (measured violins)", 40, y + 50);
    drawLabel (g, "Strings", 40, y + 282);
    drawLabel (g, "Rosin", 40, y + 342);
    drawLabel (g, "Bow", 40, y + 402);
    drawLabel (g, "Hiss", 252, y + 402);
    drawLabel (g, "Instrument (one click)", 40, y + 224);
    drawLabel (g, "Tuning", 40, y + 462);
    {
        const auto status = tuningStatus();
        g.saveState();
        g.reduceClipRegion (272, juce::roundToInt (y + 512), 96, 30);
        drawText (g,
                  status.upToFirstOccurrenceOf ("\n", false, false),
                  272,
                  y + 524,
                  Fonts::sans (10.5f),
                  colours::muted);
        drawText (g,
                  status.fromFirstOccurrenceOf ("\n", false, false),
                  272,
                  y + 538,
                  Fonts::sans (10.5f),
                  colours::dim);
        g.restoreState();
    }
    drawText (g, "Every option swaps a physical part.", 40, y + 556, Fonts::sans (11.5f), colours::dim);
    drawText (g, "There is no EQ anywhere in Octavio 2.", 40, y + 570, Fonts::sans (11.5f), colours::dim);

    // bridge and resonance
    drawPanel (g, { 396, y, 360, 576 }, "Bridge and resonance");
    drawLabel (g, "Mute", 412, y + 310);
    drawLabel (g, "Quality", 412, y + 372);
    drawText (g, "Eco arrives with the optimisation milestone.", 412, y + 428, Fonts::sans (11.5f), colours::dim);
    paintScope (g, { 412, y + 470, 328, 90 });

    // microphones and room
    drawPanel (g, { 768, y, 408, 576 }, "Microphones and room");
    paintMics (g, 768, y);
    drawLabel (g, "Room", 784, y + 306);
}

void ToneView::paintScope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& T = processor.getTelemetry();
    const int note = T.note.load();
    const int s = T.string.load();
    drawLabel (g, juce::String ("String motion (") + "GDAE"[juce::jlimit (0, 3, s)] + ")", r.getX(), r.getY() - 10);
    g.setColour (colours::well);
    g.fillRoundedRectangle (r, 6);
    const float slips = T.slips.load();
    const bool clean = slips < 1.5f;
    if (note < 0)
    {
        drawText (g,
                  "play a note",
                  r.getRight() - 8,
                  r.getY() + 18,
                  Fonts::sans (10.5f),
                  colours::dim,
                  juce::Justification::right);
        return;
    }
    drawText (g,
              clean ? juce::String ("clean Helmholtz") : juce::String (juce::roundToInt (slips)) + " slips per period",
              r.getRight() - 8,
              r.getY() + 18,
              Fonts::sans (10.5f),
              clean ? colours::good : colours::coral,
              juce::Justification::right);
    // three periods of the bridge force, from a rising zero crossing
    const auto& engine = processor.getEngine();
    const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    const int period = juce::jlimit (8, 1200, (int) std::lround (o2::Engine::rate / hz));
    const int length = 3 * period;
    const int64_t end = engine.scopeCount();
    if (end < length * 2 + 16)
        return;
    int64_t start = end - length - 1;
    for (int64_t k = end - length - 1; k > end - length - 1 - period - 2; --k)
        if (engine.scopeSample (k - 1) < 0 && engine.scopeSample (k) >= 0)
        {
            start = k;
            break;
        }
    float peak = 1e-6f;
    scope.resize ((size_t) length);
    for (int k = 0; k < length; ++k)
    {
        scope[(size_t) k] = engine.scopeSample (start + k);
        peak = std::max (peak, std::abs (scope[(size_t) k]));
    }
    juce::Path p;
    for (int k = 0; k < length; ++k)
    {
        const float px = r.getX() + 8 + (r.getWidth() - 16) * k / (float) (length - 1);
        const float py = r.getCentreY() + 6 - scope[(size_t) k] / peak * (r.getHeight() / 2 - 14);
        k == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
    }
    g.setColour (clean ? colours::good : colours::coral);
    g.strokePath (p, juce::PathStrokeType (1.8f));
}

// ---------------------------------------------------------------- M7 tuning
juce::String ToneView::tuningStatus() const
{
    const auto system = intonation.getSelectedItemIndex();
    if (scalaError.isNotEmpty())
        return "Scala error\n" + scalaError;
    if (system == o2::intonScala)
    {
        const auto name = processor.getScalaName();
        return name.isEmpty() ? juce::String ("no scale loaded\nplays equal") : "scale\n" + name;
    }
    if (system == o2::intonMts)
        return processor.mtsHasMaster() ? "MTS-ESP master\n" + processor.mtsScaleName()
                                        : juce::String ("no MTS-ESP master\nplays equal");
    const auto name = processor.getScalaName();
    return name.isEmpty() ? juce::String() : "Scala loaded\n" + name;
}

void ToneView::chooseScala()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a Scala scale (.scl) and optionally its keyboard map (.kbm)",
                                                   juce::File(),
                                                   "*.scl;*.kbm");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              juce::File scl, kbm;
                              for (const auto& f : fc.getResults())
                                  (f.hasFileExtension ("kbm") ? kbm : scl) = f;
                              if (scl == juce::File())
                                  return;
                              scalaError = processor.loadScala (scl, kbm);
                              if (scalaError.isEmpty())
                                  if (auto* param
                                      = processor.getParameters().getParameter (params::id::intonation.getParamID()))
                                  {
                                      param->beginChangeGesture();
                                      param->setValueNotifyingHost (param->convertTo0to1 ((float) o2::intonScala));
                                      param->endChangeGesture();
                                  }
                              repaint();
                          });
}

ValueBox::ValueBox (juce::RangedAudioParameter& p, std::function<juce::String (float)> t, std::vector<float> pr)
    : param (p),
      attachment (p,
                  [this] (float v)
                  {
                      value = v;
                      repaint();
                  }),
      text (std::move (t)),
      presets (std::move (pr))
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ValueBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (colours::panel2);
    g.fillRoundedRectangle (r, 7);
    g.setColour (dragging ? colours::amber : colours::line);
    g.drawRoundedRectangle (r, 7, 1);
    drawText (g, text (value), 12, r.getCentreY() + 5, Fonts::sans (13), colours::text);
}

void ValueBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        for (float v : presets)
            menu.addItem (text (v), [this, v] { attachment.setValueAsCompleteGesture (v); });
        menu.addSeparator();
        menu.addItem ("Host menu...", [this] { showParameterMenu (*this, param); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        return;
    }
    dragStart = value;
    dragging = true;
    attachment.beginGesture();
    repaint();
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float perPixel = e.mods.isShiftDown() ? 0.02f : 0.2f;
    const auto& range = param.getNormalisableRange();
    attachment.setValueAsPartOfGesture (
        range.snapToLegalValue (dragStart - perPixel * (float) e.getDistanceFromDragStartY()));
}

void ValueBox::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        attachment.endGesture();
    dragging = false;
    repaint();
}

void ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

juce::Point<float> ToneView::micPoint (int index) const
{
    // around the violin drawn by paintMics (centre 972, 286 in design units, relative to top)
    const float cx = 768 + 204, cy = 116 - top + 186;
    const juce::Point<float> at[]
        = { { cx - 150, cy + 30 }, { cx + 76, cy - 60 }, { cx - 110, cy - 100 }, { cx + 150, cy + 20 } };
    return at[juce::jlimit (0, 3, index)];
}

int ToneView::micAt (juce::Point<float> p) const
{
    for (int k = 0; k < params::micNames().size(); ++k)
        if (p.getDistanceFrom (micPoint (k)) < 22
            || juce::Rectangle<float> (micPoint (k).x - 46, micPoint (k).y + 12, 92, 20).contains (p))
            return k;
    return -1;
}

void ToneView::mouseUp (const juce::MouseEvent& e)
{
    const int k = micAt (e.position);
    if (k >= 0 && e.mouseWasClicked())
        micAttachment->setValueAsCompleteGesture ((float) k);
}

void ToneView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (micAt (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ToneView::paintMics (juce::Graphics& g, float x3, float y)
{
    const float cx = x3 + 204, cy = y + 186;
    g.setColour (juce::Colour (0xff5b3820));
    g.fillEllipse (cx - 44, cy - 74, 88, 148);
    g.setColour (juce::Colour (0xff8a5a35));
    g.drawEllipse (cx - 44, cy - 74, 88, 148, 1);
    juce::Path ff;
    ff.startNewSubPath (cx - 30, cy - 6);
    ff.quadraticTo (cx - 42, cy + 4, cx - 30, cy + 14);
    ff.startNewSubPath (cx + 30, cy - 6);
    ff.quadraticTo (cx + 42, cy + 4, cx + 30, cy + 14);
    g.setColour (juce::Colour (0xff2a1a0f));
    g.strokePath (ff, juce::PathStrokeType (3));
    g.setColour (juce::Colour (0xff1a1310));
    g.fillRoundedRectangle (cx - 7, cy - 130, 14, 60, 3);
    g.setColour (juce::Colour (0xff3a2516));
    g.fillEllipse (cx - 8, cy - 144, 16, 16);
    drawText (g,
              "violin seen from above: click a microphone",
              cx,
              cy + 92,
              Fonts::sans (10),
              colours::dim,
              juce::Justification::horizontallyCentred);
    struct Mic
    {
        juce::String name;
        float x, y;
        bool on;
    };
    std::vector<Mic> mics;
    for (int k = 0; k < params::micNames().size(); ++k)
    {
        const auto pt = micPoint (k);
        mics.push_back ({ params::micNames()[k], pt.x, pt.y, k == mic });
    }
    for (const auto& m : mics)
    {
        if (m.on)
        {
            juce::Path beam;
            // a cone from the microphone to the violin's near side
            const juce::Point<float> c (cx, cy), at (m.x, m.y);
            const auto d = (c - at) / c.getDistanceFrom (at);
            const juce::Point<float> nrm (-d.y, d.x), base = c - d * 24.0f;
            beam.addTriangle (at, base + nrm * 34.0f, base - nrm * 34.0f);
            g.setColour (colours::amber.withAlpha (0.12f));
            g.fillPath (beam);
        }
        g.setColour (m.on ? colours::amber : colours::panel2);
        g.fillEllipse (m.x - 9, m.y - 9, 18, 18);
        g.setColour (m.on ? colours::amber : colours::dim);
        g.drawEllipse (m.x - 9, m.y - 9, 18, 18, 1.5f);
        drawText (g,
                  m.name,
                  m.x,
                  m.y + 26,
                  Fonts::sans (11, m.on),
                  m.on ? colours::text : colours::dim,
                  juce::Justification::horizontallyCentred);
    }
}
} // namespace octavio2::ui

namespace octavio2::ui
{

ToneView::Instruments::Instruments (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
}

int ToneView::Instruments::active() const
{
    return instruments::active (state); // the header's Instrument menu shares it (2.3)
}

juce::Rectangle<float> ToneView::Instruments::itemBounds (int k) const
{
    const float w = (getWidth() - 8.0f) / 2.0f;
    return { k * (w + 8.0f), 0.0f, w, (float) getHeight() };
}

void ToneView::Instruments::choose (int k)
{
    instruments::choose (state, k);
}

void ToneView::Instruments::paint (juce::Graphics& g)
{
    const int on = active();
    for (int k = 0; k < 2; ++k)
    {
        const auto r = itemBounds (k);
        if (k == on)
        {
            g.setColour (colours::amber);
            g.fillRoundedRectangle (r, 6);
        }
        else
        {
            g.setColour (colours::panel2);
            g.fillRoundedRectangle (r, 6);
            g.setColour (colours::line);
            g.drawRoundedRectangle (r.reduced (0.5f), 6, 1);
        }
        drawText (g,
                  instruments::names[k],
                  r.getCentreX(),
                  r.getCentreY() + 4.5f,
                  Fonts::sans (12, k == on),
                  k == on ? colours::bg : colours::text,
                  juce::Justification::horizontallyCentred);
    }
}

void ToneView::Instruments::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    for (int k = 0; k < 2; ++k)
        if (itemBounds (k).contains (e.position))
            choose (k);
    repaint();
}

void ToneView::Instruments::mouseMove (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
} // namespace octavio2::ui
