#include "HawthorneLookAndFeel.h"
#include "BinaryData.h"

namespace hawthorne
{

void drawLogoMark (juce::Graphics& g, juce::Rectangle<float> area)
{
    static std::unique_ptr<juce::Drawable> mark = [] () -> std::unique_ptr<juce::Drawable>
    {
        if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (BinaryData::hawthornemark_svg,
                                                                         BinaryData::hawthornemark_svgSize)))
            return juce::Drawable::createFromSVG (*xml);
        return nullptr;
    }();

    if (mark != nullptr)
        mark->drawWithin (g, area, juce::RectanglePlacement::centred, 1.0f);
}

} // namespace hawthorne
