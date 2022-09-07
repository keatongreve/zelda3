#import "mac_volume_control.h"

#import <Foundation/Foundation.h>
#import <AVFAudio/AVFAudio.h>

static AVAudioEngine *engine;
static AVAudioPlayerNode *playerNode;
static AVAudioFormat *inputFormat;
static AVAudioFormat *outputFormat;
static AVAudioConverter *converter;

void PlayAudioBuffer(void *buffer, int bufferLength, int sampleRate, int blockNumber) {
  if (!engine) {
    NSLog(@"initializing engine");
    inputFormat = [[AVAudioFormat alloc]
      initWithCommonFormat:AVAudioPCMFormatInt16 sampleRate:sampleRate channels:2 interleaved:NO];
    engine = [[AVAudioEngine alloc] init];
    engine.mainMixerNode.volume = 1.0;
    outputFormat = [engine.mainMixerNode outputFormatForBus:0];
    NSLog(@"inputFormat = %@", inputFormat);
    NSLog(@"outputFormat = %@", outputFormat);
    converter = [[AVAudioConverter alloc] initFromFormat:inputFormat toFormat:outputFormat];
    playerNode = [[AVAudioPlayerNode alloc] init];
    [engine attachNode:playerNode];
    [engine connect:playerNode to:engine.mainMixerNode format:outputFormat];
    [engine prepare];
    [engine startAndReturnError:nil];
  }

  AVAudioPCMBuffer *inputBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:inputFormat
     frameCapacity:bufferLength];
  inputBuffer.frameLength = bufferLength;
  int16_t *const *channelData = inputBuffer.int16ChannelData;
  memcpy(inputBuffer.mutableAudioBufferList->mBuffers[0].mData, buffer, bufferLength);
  AVAudioPCMBuffer *outputBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:outputFormat
    frameCapacity:bufferLength];

  // AVAudioConverterInputBlock block = ^AVAudioBuffer *(AVAudioPacketCount inNumberOfPackets, AVAudioConverterInputStatus *outStatus) {
  //   *outStatus = AVAudioConverterInputStatus_HaveData;
  //   //NSLog(@"sending input buffer to converter = %@", inputBuffer);
  //   return inputBuffer;
  // };

  //NSLog(@"going to call converter for outputBuffer = %@", outputBuffer);
  //[converter convertToBuffer:outputBuffer error:nil withInputFromBlock:block];
  [converter convertToBuffer:outputBuffer fromBuffer:inputBuffer error:nil];

  // NSLog(@"outputBuffer = %@", outputBuffer);

  // NSLog(@"playerNode = %@", playerNode);
  // NSLog(@"engine = %@", engine);
  // NSLog(@"inputBuffer.audioBufferList = %p", (inputBuffer.audioBufferList));
  //NSLog(@"inputBuffer bytes = %i", *((int16_t *)inputBuffer.audioBufferList->mBuffers[0].mData));
  //NSLog(@"outputBuffer bytes = %f", *((float *)outputBuffer.audioBufferList->mBuffers[0].mData));
  
  AVAudioPlayerNodeBufferOptions options;

  if (blockNumber % 10 == 0) {
    [playerNode play];
  }

  [playerNode scheduleBuffer:outputBuffer atTime:nil options:options completionHandler:^{
    //NSLog(@"complete");
  }];
}