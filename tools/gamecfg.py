"""Per-game paths/prefixes for the recompilers. Game id from env GOLGO_GAME (default golgo13, whose layout is the historical one)."""
import os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
GAME = os.environ.get('GOLGO_GAME', 'golgo13')
PRIMARY = GAME == 'golgo13'
PFX = '' if PRIMARY else GAME + '_'                                   # symbol / file prefix
REGIONS = os.path.join(ROOT, 'work', 'regions') if PRIMARY else os.path.join(ROOT, 'work', GAME)
FLOW = os.path.join(ROOT, 'work', 'flow') if PRIMARY else os.path.join(ROOT, 'work', GAME, 'flow')
H8FLOW = os.path.join(ROOT, 'work', 'h8') if PRIMARY else os.path.join(ROOT, 'work', GAME, 'h8')
OUT_MIPS = os.path.join(ROOT, 'generated' if PRIMARY else 'generated_' + GAME)
OUT_H8 = os.path.join(ROOT, 'generated_h8' if PRIMARY else 'generated_h8_' + GAME)
