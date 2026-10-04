#include "ModeBadge.h"

namespace octavio2::ui
{
namespace
{
const char* const names[o2::dimCount]
    = { "Dynamics", "Vibrato width", "Vibrato rate", "Contact point", "Bow pressure" };
const int ccs[o2::dimCount] = { 1, 26, 19, 74, 22 };
} // namespace

Mode ModeBadge::displayed (Processor& p, int d)
{
    // the mode chosen; Auto shows Guided while an incoming controller lane holds the dimension
    const int m = p.getDimMode (d);
    if (m == 0 && p.getTelemetry().ccHolds[(size_t) juce::jlimit (0, o2::dimCount - 1, d)].load())
        return Mode::guided;
    return modeOf (m);
}

const char* ModeBadge::dimName (int d)
{
    return names[juce::jlimit (0, o2::dimCount - 1, d)];
}

ModeBadge::ModeBadge (Processor& p, int d)
    : processor (&p),
      dim (juce::jlimit (0, o2::dimCount - 1, d)),
      name (names[dim])
{
    shown = [this] { return displayed (*processor, dim); };
    chooser = [this] (Mode m) { processor->setDimMode (dim, (int) m); };
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updateTooltip();
    startTimerHz (10);
}

ModeBadge::ModeBadge (std::function<Mode()> s,
                      std::function<void (Mode)> c,
                      juce::String n,
                      juce::String a,
                      juce::String gt)
    : shown (std::move (s)),
      chooser (std::move (c)),
      name (std::move (n)),
      autoText (std::move (a)),
      guidedText (std::move (gt))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    updateTooltip();
    startTimerHz (10);
}

void ModeBadge::placeAt (juce::Point<float> c)
{
    setBounds (juce::roundToInt (c.x - 11), juce::roundToInt (c.y - 11), 22, 22);
}

void ModeBadge::timerCallback()
{
    const auto m = shown();
    if (m != last)
    {
        last = m;
        updateTooltip();
        repaint();
    }
}

void ModeBadge::updateTooltip()
{
    const auto m = shown();
    juce::String t = name + ": ";
    if (dim >= 0)
    {
        const bool cc = processor->getTelemetry().ccHolds[(size_t) dim].load();
        const bool param = processor->getDimMode (dim) != 0;
        const bool live = processor->getTelemetry().dimMode[(size_t) dim].load() != 0;
        t << (m == Mode::autoMode ? juce::String ("Auto, the player decides.")
                  : m == Mode::guided
                  ? (cc && ! param
                         ? "Guided by the incoming CC" + juce::String (ccs[dim]) + ": the player shapes around it."
                         : juce::String ("Guided: your curve or CC") + juce::String (ccs[dim])
                             + " sets the level, the player shapes around it"
                             + (live ? "." : " (none here yet, so the player decides)."))
                  : "Manual: exactly your curve or CC" + juce::String (ccs[dim])
                      + (live ? "." : " (none here yet: the knobs set it)."));
        t << " Click to choose Auto, Guided or Manual.";
    }
    else
        t << (m == Mode::autoMode ? autoText : guidedText) << " Click to change.";
    setTooltip (t);
}

void ModeBadge::paint (juce::Graphics& g)
{
    const auto m = shown();
    drawBadge (g, getLocalBounds().toFloat().getCentre(), m);
    if (isMouseOver())
    {
        g.setColour (colours::text.withAlpha (0.35f));
        g.drawEllipse (getLocalBounds().toFloat().reduced (0.5f), 1.0f);
    }
}

void ModeBadge::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        showMenu();
}

void ModeBadge::choose (Mode m)
{
    if (chooser)
        chooser (m);
    last = m;
    updateTooltip();
    repaint();
}

void ModeBadge::showMenu()
{
    juce::PopupMenu menu;
    const auto now = shown();
    if (dim >= 0)
    {
        const int param = processor->getDimMode (dim);
        const bool cc = processor->getTelemetry().ccHolds[(size_t) dim].load();
        menu.addSectionHeader (name + ": who is in charge");
        menu.addItem (1, "Auto: the player decides", true, param == 0 && ! (cc && now == Mode::guided));
        menu.addItem (2,
                      "Guided: your curve or CC" + juce::String (ccs[dim]) + ", the player shapes around it",
                      true,
                      param == 1 || (param == 0 && cc && now == Mode::guided));
        menu.addItem (3, "Manual: exactly your curve or CC" + juce::String (ccs[dim]), true, param == 2);
        if (param == 0 && cc)
            menu.addItem (-1,
                          "(an incoming CC" + juce::String (ccs[dim]) + " made it Guided; Auto gives it back)",
                          false,
                          false);
    }
    else
    {
        menu.addSectionHeader (name);
        menu.addItem (1, "Auto: " + autoText, true, now == Mode::autoMode);
        menu.addItem (2, "Guided: " + guidedText, true, now == Mode::guided);
    }
    juce::Component::SafePointer<ModeBadge> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe] (int r)
                        {
                            if (safe != nullptr && r > 0)
                                safe->choose (r == 1 ? Mode::autoMode : r == 2 ? Mode::guided : Mode::manual);
                        });
}
} // namespace octavio2::ui
