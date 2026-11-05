"""FIFO B is stopped by VBlank IRQ, even when rendering/loading stalls.
1024 samples cover >3 VBlanks at 16384 Hz plus the FIFO/DMA prefetch.
Music keeps its long guard because loop restarts remain in the main loop.
"""
EFFECT_GUARD=1024
MUSIC_GUARD=65536
def guarded_pcm(pcm,music=False):
 return pcm+bytes((-len(pcm))%4+(MUSIC_GUARD if music else EFFECT_GUARD))
