#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class KeyEditor : public juce::AudioProcessorEditor,
                  public juce::FileDragAndDropTarget,
                  private juce::Timer
{
public:
    explicit KeyEditor (KeyProcessor&);
    ~KeyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override { repaint(); }
    float scale() const { return (float) getWidth() / 1000.0f; }
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Colour,
               juce::Justification, float kern = 0.f);
    void glow (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Justification);

    KeyProcessor& proc;
    juce::Image bg, scaledBg;
    bool dropHover = false;
    float peakHold = 0.f;
    double t0 = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyEditor)
};
