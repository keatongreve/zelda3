#import "mac_volume_control.h"

#include <Accelerate/Accelerate.h>  
#import <Foundation/Foundation.h>
#import <AVFAudio/AVFAudio.h>

static AVAudioEngine *engine;
static AVAudioPlayerNode *playerNode;
static AVAudioFormat *outputFormat;

void PlayAudioBuffer(void *buffer, int numberFrames, int numberChannels, int sampleRate) {
  if (!engine) {
    engine = [[AVAudioEngine alloc] init];
    engine.mainMixerNode.volume = 1.0;
    //outputFormat = [engine.mainMixerNode outputFormatForBus:0];
    outputFormat = [[AVAudioFormat alloc]
        initWithCommonFormat:AVAudioPCMFormatFloat32
        sampleRate:sampleRate
        channels:numberChannels
        interleaved:NO];
    playerNode = [[AVAudioPlayerNode alloc] init];
    [engine attachNode:playerNode];
    [engine connect:playerNode to:engine.mainMixerNode format:outputFormat];
    [engine prepare];
    [engine startAndReturnError:nil];
    [playerNode play];
  }

  float *convertedSamples = malloc(numberFrames * numberChannels * sizeof(float));
  vDSP_vflt16((int16_t *)buffer, 1, convertedSamples, 1, numberFrames * numberChannels);
  float div = 32767.0;
  vDSP_vsdiv(convertedSamples, 1, &div, convertedSamples, 1, numberFrames * numberChannels);
;
  __block AVAudioPCMBuffer *outputBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:outputFormat
    frameCapacity:numberFrames];
  outputBuffer.frameLength = numberFrames;
  float *const *channelData = outputBuffer.floatChannelData;
  for (int channel = 0; channel < numberChannels; channel++) {
    for (int frame = 0; frame < numberFrames; frame++) {
      channelData[channel][frame] = convertedSamples[frame * 2 + channel];
    }
  }

  AVAudioPlayerNodeBufferOptions options;
  NSLog(@"format = %@", outputFormat);

  [playerNode scheduleBuffer:outputBuffer atTime:nil options:options completionHandler:^{
    free(*(outputBuffer.floatChannelData));
    outputBuffer = nil;
  }];
  free(convertedSamples);
}