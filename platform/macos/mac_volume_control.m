#import "mac_volume_control.h"

#include <Accelerate/Accelerate.h>  
#import <Foundation/Foundation.h>
#import <AVFAudio/AVFAudio.h>

static AVAudioEngine *engine;
static AVAudioPlayerNode *playerNode;
static AVAudioFormat *outputFormat;
static AVAudioUnitVarispeed *speedControl;

//void PlayAudioBuffer(void *buffer, int bufferLength, int sampleRate, int blockNumber) {
void PlayAudioBuffer(void *buffer, int numberFrames, int sampleRate) {
  if (!engine) {
    engine = [[AVAudioEngine alloc] init];
    engine.mainMixerNode.volume = 1.0;
    outputFormat = [engine.mainMixerNode outputFormatForBus:0];
    playerNode = [[AVAudioPlayerNode alloc] init];
    speedControl = [[AVAudioUnitVarispeed alloc] init];
    speedControl.rate = 2.0f;
    [engine attachNode:playerNode];
    [engine attachNode:speedControl];
    [engine connect:playerNode to:speedControl format:nil];
    [engine connect:speedControl to:engine.mainMixerNode format:outputFormat];
    [engine prepare];
    [engine startAndReturnError:nil];
    [playerNode play];
  }

  float *convertedSamples = malloc(numberFrames * sizeof(float));
  vDSP_vflt16((int16_t *)buffer, 1, convertedSamples, 1, numberFrames);
  float div = 32767.0;
  vDSP_vsdiv(convertedSamples, 1, &div, convertedSamples, 1, numberFrames);

  __block AVAudioPCMBuffer *outputBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:outputFormat
     frameCapacity:numberFrames];
  outputBuffer.frameLength = numberFrames;
  float *const *channelData = outputBuffer.floatChannelData;
  memcpy((void *)*channelData, convertedSamples, numberFrames * sizeof(float));
  
  AVAudioPlayerNodeBufferOptions options;

  [playerNode scheduleBuffer:outputBuffer atTime:nil options:options completionHandler:^{
    free(*(outputBuffer.floatChannelData));
    outputBuffer = nil;
  }];
  free(convertedSamples);
}