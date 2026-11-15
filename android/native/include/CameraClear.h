#pragma once

// How far past the map's edges the Android app lets the main map's camera go (android/TOUCH_UI.md, "Past the map's edges").
// The game's panels lie over the view along its edges, and at the phone's HUD factor they hid the map's edges where the
// engine holds the camera (CameraImpl::ClampTargetPos). The app measures what the panels cover (Measure, from FAF's controls:
// src/host/GriffAndroid.cpp), and the camera's clamp, by its build-time patch
// (patches/src/moho/render/camera/CameraImpl.cpp), lets the focus go so far that each edge of the map can come out from
// under them (Allowed). The rules alone, for their tests (tests/wx/CameraClearTests.cpp).

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <vector>

namespace cameraclear
{
  /// The parts of the main map's view that the game's panels cover from its left, top, right and bottom edge: 0 to 1 of the
  /// view's width or height.
  struct Margins
  {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;

    [[nodiscard]] bool None() const
    {
      return !(left > 0 || top > 0 || right > 0 || bottom > 0);
    }
  };

  /// The margins the world camera's clamp goes by, as the app last measured them (none: the engine's own clamp). Set and read
  /// on the program's thread.
  inline Margins gWorldCamera;

  /// A box on the picture, in its pixels.
  struct Rect
  {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
  };

  /// How the panels along the view's edges are told, in the picture's pixels or as parts of the view.
  struct Rules
  {
    /// Pieces that overlap by more than this each way are one panel (Panels).
    float overlap = 0;
    /// A panel within this of an edge stands at it.
    float touch = 0;
    /// A panel past an edge by more than this is on its way: FAF slides its panels in as a game begins.
    float past = 0;
    /// A panel that covers more of the view than this part from each edge it stands at is none, as a dialog's shadow.
    float most = 0;
  };

  /// The panels that the controls of the game's UI over the view make up: pieces that overlap by more than `overlap` each way,
  /// also through others, are one panel, the box about them, as a bracket at the screen's edge with the body it holds and the
  /// icons on it. Panels that only meet, side by side or as FAF's unit view stands on its orders panel, are two. A piece
  /// without area covers nothing and makes no panel. In the order of their first pieces.
  [[nodiscard]] inline std::vector<Rect> Panels(const std::vector<Rect>& all, const float overlap)
  {
    std::vector<Rect> pieces;
    std::copy_if(all.begin(), all.end(), std::back_inserter(pieces), [](const Rect& piece) {
      return piece.left < piece.right && piece.top < piece.bottom;
    });
    std::vector<std::size_t> joined(pieces.size());
    for (std::size_t at = 0; at < pieces.size(); ++at) {
      joined[at] = at;
    }
    const auto root = [&joined](std::size_t at) {
      while (joined[at] != at) {
        joined[at] = joined[joined[at]];
        at = joined[at];
      }
      return at;
    };
    for (std::size_t one = 0; one < pieces.size(); ++one) {
      const Rect& a = pieces[one];
      for (std::size_t other = one + 1; other < pieces.size(); ++other) {
        const Rect& b = pieces[other];
        if ((std::min)(a.right, b.right) - (std::max)(a.left, b.left) > overlap
            && (std::min)(a.bottom, b.bottom) - (std::max)(a.top, b.top) > overlap) {
          joined[root(other)] = root(one);
        }
      }
    }
    std::vector<Rect> panels;
    std::vector<std::size_t> panelOf(pieces.size(), pieces.size());
    for (std::size_t at = 0; at < pieces.size(); ++at) {
      const Rect& piece = pieces[at];
      std::size_t& panel = panelOf[root(at)];
      if (panel == pieces.size()) {
        panel = panels.size();
        panels.push_back(piece);
        continue;
      }
      Rect& box = panels[panel];
      box = Rect{(std::min)(box.left, piece.left), (std::min)(box.top, piece.top), (std::max)(box.right, piece.right),
                 (std::max)(box.bottom, piece.bottom)};
    }
    return panels;
  }

  /// What the panels along the view's edges cover of it (`rules`). Each that stands at an edge counts for the edge it reaches
  /// in from least, as a part of the view's width or height: a panel in a corner, as the construction panel at the bottom
  /// right, for the edge it lies along. One that covers too much of the view counts for none, as a dialog's shadow over the
  /// whole screen; one on its way in, sliding past an edge, neither; nor one that stands at no edge, as a window.
  [[nodiscard]] inline Margins Covered(const std::vector<Rect>& panels, const Rect& view, const Rules& rules)
  {
    Margins margins;
    const float width = view.right - view.left;
    const float height = view.bottom - view.top;
    if (!(width > 0 && height > 0)) {
      return margins;
    }
    const float touch = rules.touch;
    const float past = rules.past;
    for (const Rect& cover : panels) {
      if (!(cover.left < cover.right && cover.top < cover.bottom) || cover.left < view.left - past || cover.top < view.top - past
          || cover.right > view.right + past || cover.bottom > view.bottom + past) {
        continue;
      }
      // How far in from each edge it reaches, where it touches that edge.
      const float in[4] = {
        cover.left <= view.left + touch ? (cover.right - view.left) / width : 2.0f,
        cover.top <= view.top + touch ? (cover.bottom - view.top) / height : 2.0f,
        cover.right >= view.right - touch ? (view.right - cover.left) / width : 2.0f,
        cover.bottom >= view.bottom - touch ? (view.bottom - cover.top) / height : 2.0f,
      };
      const float* const least = std::min_element(in, in + 4);
      if (!(*least <= rules.most)) {
        continue;
      }
      float* const margin[4] = {&margins.left, &margins.top, &margins.right, &margins.bottom};
      float& edge = *margin[least - in];
      edge = (std::max)(edge, *least);
    }
    return margins;
  }

  /// What the game's UI over the view, piece by piece, covers of it along its edges: its panels (Panels), as they stand
  /// (Covered).
  [[nodiscard]] inline Margins Measure(const std::vector<Rect>& pieces, const Rect& view, const Rules& rules)
  {
    return Covered(Panels(pieces, rules.overlap), view, rules);
  }

  /// The larger of two margins at each edge: what the panels covered at any time, as they come and go with the selection.
  [[nodiscard]] inline Margins Larger(const Margins& one, const Margins& other)
  {
    return Margins{(std::max)(one.left, other.left), (std::max)(one.top, other.top), (std::max)(one.right, other.right),
                   (std::max)(one.bottom, other.bottom)};
  }

  /// The corners of the view clear of the margins, as parts of its width and height from its top left: left top, right top,
  /// left bottom, right bottom.
  struct Corner
  {
    float x = 0;
    float y = 0;
  };

  [[nodiscard]] inline std::vector<Corner> ClearCorners(const Margins& margins)
  {
    const float left = margins.left;
    const float right = 1 - margins.right;
    const float top = margins.top;
    const float bottom = 1 - margins.bottom;
    return {{left, top}, {right, top}, {left, bottom}, {right, bottom}};
  }

  /// Where on the ground a point of the view shows, from the camera's focus, in the world's x and z; `shown` false where the
  /// view shows no ground there.
  struct Ground
  {
    bool shown = false;
    float x = 0;
    float z = 0;
  };

  /// How near the camera's focus may come to the map's edge at its least and most x and z.
  struct Spans
  {
    float minX = 0;
    float maxX = 0;
    float minZ = 0;
    float maxZ = 0;
  };

  /// How near the focus may come to each edge of the map: the engine's own spans (`spanX`, `spanZ`: half of the view that its
  /// clamp keeps over the map at the zoom), or less where that keeps the edge under a panel. `corners` is the ground under the
  /// clear view's corners (ClearCorners), from the focus: the edge comes as far in as the nearest of the corners that lie
  /// towards it, so that all of the edge the clear view spans lies in it, also where the view shows less ground (the near
  /// rows of a tilted camera). Never more than the engine's spans, and the engine's where a corner shows no ground.
  [[nodiscard]] inline Spans Allowed(const float spanX, const float spanZ, const std::vector<Ground>& corners)
  {
    Spans spans{spanX, spanX, spanZ, spanZ};
    if (std::any_of(corners.begin(), corners.end(), [](const Ground& corner) { return !corner.shown; })) {
      return spans;
    }
    const auto reach = [&corners](const float towardX, const float towardZ) {
      float nearest = std::numeric_limits<float>::infinity();
      for (const Ground& corner : corners) {
        const float toward = corner.x * towardX + corner.z * towardZ;
        if (toward > 0) {
          nearest = (std::min)(nearest, toward);
        }
      }
      return nearest;
    };
    spans.minX = (std::min)(spanX, reach(-1, 0));
    spans.maxX = (std::min)(spanX, reach(1, 0));
    spans.minZ = (std::min)(spanZ, reach(0, -1));
    spans.maxZ = (std::min)(spanZ, reach(0, 1));
    return spans;
  }
} // namespace cameraclear
