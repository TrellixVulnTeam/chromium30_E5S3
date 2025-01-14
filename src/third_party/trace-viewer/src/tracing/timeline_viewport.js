// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

/**
 * @fileoverview Code for the viewport.
 */
base.require('base.events');
base.require('tracing.draw_helpers');

base.exportTo('tracing', function() {

  /**
   * The TimelineViewport manages the transform used for navigating
   * within the timeline. It is a simple transform:
   *   x' = (x+pan) * scale
   *
   * The timeline code tries to avoid directly accessing this transform,
   * instead using this class to do conversion between world and viewspace,
   * as well as the math for centering the viewport in various interesting
   * ways.
   *
   * @constructor
   * @extends {base.EventTarget}
   */
  function TimelineViewport(parentEl) {
    this.parentEl_ = parentEl;
    this.modelTrackContainer_ = null;
    this.scaleX_ = 1;
    this.panX_ = 0;
    this.panY_ = 0;
    this.gridTimebase_ = 0;
    this.gridStep_ = 1000 / 60;
    this.gridEnabled_ = false;
    this.hasCalledSetupFunction_ = false;

    this.onResize_ = this.onResize_.bind(this);
    this.onModelTrackControllerScroll_ =
        this.onModelTrackControllerScroll_.bind(this);

    // The following code uses an interval to detect when the parent element
    // is attached to the document. That is a trigger to run the setup function
    // and install a resize listener.
    this.checkForAttachInterval_ = setInterval(
        this.checkForAttach_.bind(this), 250);

    this.markers = [];
    this.majorMarkPositions = [];
  }

  TimelineViewport.prototype = {
    __proto__: base.EventTarget.prototype,

    /**
     * Allows initialization of the viewport when the viewport's parent element
     * has been attached to the document and given a size.
     * @param {Function} fn Function to call when the viewport can be safely
     * initialized.
     */
    setWhenPossible: function(fn) {
      this.pendingSetFunction_ = fn;
    },

    /**
     * @return {boolean} Whether the current timeline is attached to the
     * document.
     */
    get isAttachedToDocument_() {
      var cur = this.parentEl_;
      // Allow not providing a parent element, used by tests.
      if (cur === undefined)
        return;
      while (cur.parentNode)
        cur = cur.parentNode;
      return cur == this.parentEl_.ownerDocument;
    },

    onResize_: function() {
      this.dispatchChangeEvent();
    },

    /**
     * Checks whether the parentNode is attached to the document.
     * When it is, it installs the iframe-based resize detection hook
     * and then runs the pendingSetFunction_, if present.
     */
    checkForAttach_: function() {
      if (!this.isAttachedToDocument_ || this.clientWidth == 0)
        return;

      if (!this.iframe_) {
        this.iframe_ = document.createElement('iframe');
        this.iframe_.style.cssText =
            'position:absolute;width:100%;height:0;border:0;visibility:hidden;';
        this.parentEl_.appendChild(this.iframe_);

        this.iframe_.contentWindow.addEventListener('resize', this.onResize_);
      }

      var curSize = this.parentEl_.clientWidth + 'x' +
          this.parentEl_.clientHeight;
      if (this.pendingSetFunction_) {
        this.lastSize_ = curSize;
        try {
          this.pendingSetFunction_();
        } catch (ex) {
          console.log('While running setWhenPossible:',
              ex.message ? ex.message + '\n' + ex.stack : ex.stack);
        }
        this.pendingSetFunction_ = undefined;
      }

      window.clearInterval(this.checkForAttachInterval_);
      this.checkForAttachInterval_ = undefined;
    },

    /**
     * Fires the change event on this viewport. Used to notify listeners
     * to redraw when the underlying model has been mutated.
     */
    dispatchChangeEvent: function() {
      base.dispatchSimpleEvent(this, 'change');
    },

    dispatchMarkersChangeEvent_: function() {
      base.dispatchSimpleEvent(this, 'markersChange');
    },

    detach: function() {
      if (this.checkForAttachInterval_) {
        window.clearInterval(this.checkForAttachInterval_);
        this.checkForAttachInterval_ = undefined;
      }
      if (this.iframe_) {
        this.iframe_.removeEventListener('resize', this.onResize_);
        this.parentEl_.removeChild(this.iframe_);
      }
    },

    getStateInViewCoordinates: function() {
      return {
        panX: this.xWorldVectorToView(this.panX),
        panY: this.panY,
        scaleX: this.scaleX
      };
    },

    setStateInViewCoordinates: function(state) {
      this.panX = this.xViewVectorToWorld(state.panX);
      this.panY = state.panY;
    },

    onModelTrackControllerScroll_: function(e) {
      this.panY_ = this.modelTrackContainer_.scrollTop;
    },

    set modelTrackContainer(m) {

      if (this.modelTrackContainer_)
        this.modelTrackContainer_.removeEventListener('scroll',
            this.onModelTrackControllerScroll_);

      this.modelTrackContainer_ = m;
      this.modelTrackContainer_.addEventListener('scroll',
          this.onModelTrackControllerScroll_);
    },

    get scaleX() {
      return this.scaleX_;
    },
    set scaleX(s) {
      var changed = this.scaleX_ != s;
      if (changed) {
        this.scaleX_ = s;
        this.dispatchChangeEvent();
      }
    },

    get panX() {
      return this.panX_;
    },
    set panX(p) {
      var changed = this.panX_ != p;
      if (changed) {
        this.panX_ = p;
        this.dispatchChangeEvent();
      }
    },

    get panY() {
      return this.panY_;
    },
    set panY(p) {
      this.panY_ = p;
      this.modelTrackContainer_.scrollTop = p;
    },

    setPanAndScale: function(p, s) {
      var changed = this.scaleX_ != s || this.panX_ != p;
      if (changed) {
        this.scaleX_ = s;
        this.panX_ = p;
        this.dispatchChangeEvent();
      }
    },

    xWorldToView: function(x) {
      return (x + this.panX_) * this.scaleX_;
    },

    xWorldVectorToView: function(x) {
      return x * this.scaleX_;
    },

    xViewToWorld: function(x) {
      return (x / this.scaleX_) - this.panX_;
    },

    xViewVectorToWorld: function(x) {
      return x / this.scaleX_;
    },

    xPanWorldPosToViewPos: function(worldX, viewX, viewWidth) {
      if (typeof viewX == 'string') {
        if (viewX == 'left') {
          viewX = 0;
        } else if (viewX == 'center') {
          viewX = viewWidth / 2;
        } else if (viewX == 'right') {
          viewX = viewWidth - 1;
        } else {
          throw new Error('unrecognized string for viewPos. left|center|right');
        }
      }
      this.panX = (viewX / this.scaleX_) - worldX;
    },

    xPanWorldBoundsIntoView: function(worldMin, worldMax, viewWidth) {
      if (this.xWorldToView(worldMin) < 0)
        this.xPanWorldPosToViewPos(worldMin, 'left', viewWidth);
      else if (this.xWorldToView(worldMax) > viewWidth)
        this.xPanWorldPosToViewPos(worldMax, 'right', viewWidth);
    },

    xSetWorldBounds: function(worldMin, worldMax, viewWidth) {
      var worldWidth = worldMax - worldMin;
      var scaleX = viewWidth / worldWidth;
      var panX = -worldMin;
      this.setPanAndScale(panX, scaleX);
    },

    get gridEnabled() {
      return this.gridEnabled_;
    },

    set gridEnabled(enabled) {
      if (this.gridEnabled_ == enabled)
        return;

      this.gridEnabled_ = enabled && true;
      this.dispatchChangeEvent();
    },

    get gridTimebase() {
      return this.gridTimebase_;
    },

    set gridTimebase(timebase) {
      if (this.gridTimebase_ == timebase)
        return;
      this.gridTimebase_ = timebase;
      this.dispatchChangeEvent();
    },

    get gridStep() {
      return this.gridStep_;
    },

    applyTransformToCanvas: function(ctx) {
      ctx.transform(this.scaleX_, 0, 0, 1, this.panX_ * this.scaleX_, 0);
    },

    createMarker: function(positionWorld) {
      return new ViewportMarker(this, positionWorld);
    },

    addMarker: function(marker) {
      if (this.markers.indexOf(marker) >= 0)
        return false;
      this.markers.push(marker);
      this.dispatchChangeEvent();
      this.dispatchMarkersChangeEvent_();
      return marker;
    },

    removeMarker: function(marker) {
      for (var i = 0; i < this.markers.length; ++i) {
        if (this.markers[i] === marker) {
          this.markers.splice(i, 1);
          this.dispatchChangeEvent();
          this.dispatchMarkersChangeEvent_();
          return true;
        }
      }
    },

    findMarkerNear: function(positionWorld, nearnessInViewPixels) {
      // Converts pixels into distance in world.
      var nearnessThresholdWorld = this.xViewVectorToWorld(
          nearnessInViewPixels);
      for (var i = 0; i < this.markers.length; ++i) {
        if (Math.abs(this.markers[i].positionWorld - positionWorld) <=
            nearnessThresholdWorld) {
          var marker = this.markers[i];
          return marker;
        }
      }
      return undefined;
    },

    drawMarkLines: function(ctx) {
      // Apply subpixel translate to get crisp lines.
      // http://www.mobtowers.com/html5-canvas-crisp-lines-every-time/
      ctx.save();
      ctx.translate((Math.round(ctx.lineWidth) % 2) / 2, 0);

      ctx.beginPath();
      for (var idx in this.majorMarkPositions) {
        var x = Math.floor(this.majorMarkPositions[idx]);
        tracing.drawLine(ctx, x, 0, x, ctx.canvas.height);
      }
      ctx.strokeStyle = '#ddd';
      ctx.stroke();

      ctx.restore();
    },

    drawGridLines: function(ctx, viewLWorld, viewRWorld) {
      if (!this.gridEnabled)
        return;

      var x = this.gridTimebase;

      // Apply subpixel translate to get crisp lines.
      // http://www.mobtowers.com/html5-canvas-crisp-lines-every-time/
      ctx.save();
      ctx.translate((Math.round(ctx.lineWidth) % 2) / 2, 0);

      ctx.beginPath();
      while (x < viewRWorld) {
        if (x >= viewLWorld) {
          // Do conversion to viewspace here rather than on
          // x to avoid precision issues.
          var vx = Math.floor(this.xWorldToView(x));
          tracing.drawLine(ctx, vx, 0, vx, ctx.canvas.height);
        }

        x += this.gridStep;
      }
      ctx.strokeStyle = 'rgba(255, 0, 0, 0.25)';
      ctx.stroke();

      ctx.restore();
    },

    drawMarkerArrows: function(ctx, viewLWorld, viewRWorld, drawHeight) {
      for (var i = 0; i < this.markers.length; ++i) {
        var marker = this.markers[i];
        var ts = marker.positionWorld;
        if (ts < viewLWorld || ts > viewRWorld)
          continue;
        marker.drawArrow(ctx, drawHeight);
      }
    },

    drawMarkerLines: function(ctx, viewLWorld, viewRWorld) {
      // Dim the area left and right of the markers if there are 2 markers.
      if (this.markers.length === 2) {
        var posWorld0 = this.markers[0].positionWorld;
        var posWorld1 = this.markers[1].positionWorld;

        var markerLWorld = Math.min(posWorld0, posWorld1);
        var markerRWorld = Math.max(posWorld0, posWorld1);

        var markerLView = this.xWorldToView(markerLWorld);
        var markerRView = this.xWorldToView(markerRWorld);

        ctx.fillStyle = 'rgba(0, 0, 0, 0.2)';
        if (markerLWorld > viewLWorld) {
          ctx.fillRect(this.xWorldToView(viewLWorld), 0,
              markerLView, ctx.canvas.height);
        }

        if (markerRWorld < viewRWorld) {
          ctx.fillRect(markerRView, 0,
              this.xWorldToView(viewRWorld), ctx.canvas.height);
        }
      }

      var pixelRatio = window.devicePixelRatio || 1;
      ctx.lineWidth = Math.round(pixelRatio);

      for (var i = 0; i < this.markers.length; ++i) {
        var marker = this.markers[i];

        var ts = marker.positionWorld;
        if (ts < viewLWorld || ts >= viewRWorld)
          continue;

        marker.drawLine(ctx, ctx.canvas.height);
      }

      ctx.lineWidth = 1;
    }
  };

  /**
   * Represents a marked position in the world, at a viewport level.
   * @constructor
   */
  function ViewportMarker(vp, positionWorld) {
    this.viewport_ = vp;
    this.positionWorld_ = positionWorld;
    this.selected_ = false;
  }

  ViewportMarker.prototype = {
    get positionWorld() {
      return this.positionWorld_;
    },

    set positionWorld(positionWorld) {
      this.positionWorld_ = positionWorld;
      this.viewport_.dispatchChangeEvent();
    },

    get positionView() {
      return this.viewport_.xWorldToView(this.positionWorld);
    },

    set selected(selected) {
      if (this.selected === selected)
        return;
      this.selected_ = selected;
      this.viewport_.dispatchChangeEvent();
    },

    get selected() {
      return this.selected_;
    },

    get color() {
      return this.selected ? 'rgb(255, 0, 0)' : 'rgb(0, 0, 0)';
    },

    drawArrow: function(ctx, height) {
      var viewX = this.viewport_.xWorldToView(this.positionWorld_);

      // Apply subpixel translate to get crisp lines.
      // http://www.mobtowers.com/html5-canvas-crisp-lines-every-time/
      ctx.save();
      ctx.translate((Math.round(ctx.lineWidth) % 2) / 2, 0);

      tracing.drawTriangle(ctx,
          viewX, height,
          viewX - 3, height / 2,
          viewX + 3, height / 2);
      ctx.fillStyle = this.color;
      ctx.fill();

      ctx.restore();
    },

    drawLine: function(ctx, height) {
      var viewX = this.viewport_.xWorldToView(this.positionWorld_);

      // Apply subpixel translate to get crisp lines.
      // http://www.mobtowers.com/html5-canvas-crisp-lines-every-time/
      ctx.save();
      ctx.translate((Math.round(ctx.lineWidth) % 2) / 2, 0);

      ctx.beginPath();
      tracing.drawLine(ctx, viewX, 0, viewX, height);
      ctx.strokeStyle = this.color;
      ctx.stroke();

      ctx.restore();
    }
  };

  return {
    TimelineViewport: TimelineViewport,
    ViewportMarker: ViewportMarker
  };
});
