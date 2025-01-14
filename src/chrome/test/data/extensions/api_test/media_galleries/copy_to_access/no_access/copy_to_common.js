// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var mediaGalleries = chrome.mediaGalleries;

function getOnwriteendCallbackExpectFailure(testImageFileEntry, galleries) {
  return function() {
    testImageFileEntry.copyTo(galleries[0].root, this.filename,
        chrome.test.fail,
        chrome.test.callbackPass(function() {}));
  };
}

function getOnwriteendCallbackExpectSuccess(testImageFileEntry, galleries) {
  return function() {
    testImageFileEntry.copyTo(galleries[0].root, this.filename,
        chrome.test.callbackPass(function() {}),
        chrome.test.fail);
  };
}

// Valid WEBP image.
var validCase = {
  filename: "valid.webp",
  blobString: "RIFF0\0\0\0WEBPVP8 $\0\0\0\xB2\x02\0\x9D\x01\x2A" +
              "\x01\0\x01\0\x2F\x9D\xCE\xE7s\xA8((((\x01\x9CK(\0" +
              "\x05\xCE\xB3l\0\0\xFE\xD8\x80\0\0"
}

// Write an invalid test image and expect failure.
var invalidCase = {
  filename: "invalid.webp",
  blobString: "abc123"
}

function runTest(testCase, getOnwriteendCallback) {
  var galleries;
  var testImageFileEntry;

  function getMediaFileSystemsList() {
    mediaGalleries.getMediaFileSystems(
        chrome.test.callbackPass(testGalleries));
  }

  function testGalleries(results) {
    galleries = results;
    chrome.test.assertTrue(galleries.length > 0,
                           "Need at least one media gallery to test copyTo");

    // Create a temporary file system and an image for copying-into test.
    window.webkitRequestFileSystem(window.TEMPORARY, 1024*1024,
        chrome.test.callbackPass(temporaryFileSystemCallback),
        chrome.test.fail);
  }

  function temporaryFileSystemCallback(filesystem) {
    filesystem.root.getFile(testCase.filename, {create:true, exclusive: false},
        chrome.test.callbackPass(temporaryImageCallback),
        chrome.test.fail);
  }

  function temporaryImageCallback(entry) {
    testImageFileEntry = entry;
    entry.createWriter(
        chrome.test.callbackPass(imageWriterCallback),
        chrome.test.fail);
  }

  function imageWriterCallback(writer) {
    // Go through a Uint8Array to avoid UTF-8 control bytes.
    var blobBytes = new Uint8Array(testCase.blobString.length);
    for (var i = 0; i < testCase.blobString.length; i++) {
      blobBytes[i] = testCase.blobString.charCodeAt(i);
    }
    var blob = new Blob([blobBytes], {type : "image/webp"});

    writer.onerror = function(e) {
      chrome.test.fail("Unable to write test image: " + e.toString());
    }

    writer.onwriteend = chrome.test.callbackPass(
      getOnwriteendCallback(testImageFileEntry, galleries));

    writer.write(blob);
  }

  getMediaFileSystemsList();
}

function createTest(testCase, getOnwriteendCallback) {
  return function() {
    runTest(testCase, getOnwriteendCallback);
  };
}
