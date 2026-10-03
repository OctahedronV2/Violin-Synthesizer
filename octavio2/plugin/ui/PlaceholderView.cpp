#include "PlaceholderView.h"

namespace octavio2::ui
{
PlaceholderView::PlaceholderView (juce::String t, juce::String w, juce::String n)
    : title (std::move (t)),
      what (std::move (w)),
      when (std::move (n))
{
}

void PlaceholderView::paint (juce::Graphics& g)
{
    const juce::Rectangle<float> r (24, 12, designWidth - 48, 576);
    drawPanel (g, r, title);
    drawText (g,
              title,
              r.getCentreX(),
              r.getCentreY() - 30,
              Fonts::serif (40),
              colours::text,
              juce::Justification::horizontallyCentred);
    g.setColour (colours::muted);
    g.setFont (Fonts::sans (14));
    g.drawFittedText (what,
                      r.withSizeKeepingCentre (620, 60).translated (0, 20).toNearestInt(),
                      juce::Justification::centredTop,
                      3);
    drawText (g,
              when,
              r.getCentreX(),
              r.getCentreY() + 90,
              Fonts::sans (12),
              colours::dim,
              juce::Justification::horizontallyCentred);
}
} // namespace octavio2::ui
