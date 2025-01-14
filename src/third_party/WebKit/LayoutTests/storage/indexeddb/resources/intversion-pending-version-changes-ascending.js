if (this.importScripts) {
    importScripts('../../../fast/js/resources/js-test-pre.js');
    importScripts('shared.js');
}

description("Check processing of pending version change requests - increasing versions.");

indexedDBTest(null, function onConnection1Open(evt) {
    preamble(evt);
    db = event.target.result;

    request = evalAndLog("indexedDB.open(dbname, 2)");
    request.onblocked = connection2Blocked;
    request.onupgradeneeded = connection2UpgradeNeeded;
    request.onsuccess = connection2OpenSuccess;
    request.onerror = unexpectedErrorCallback;

    request = evalAndLog("indexedDB.open(dbname, 3)");
    request.onblocked = connection3Blocked;
    request.onupgradeneeded = connection3UpgradeNeeded;
    request.onsuccess = connection3OpenSuccess;
    request.onerror = unexpectedErrorCallback;

    debug("");
    debug("FIXME: The open call with higher version should execute first.");
});

function connection2Blocked(evt)
{
    preamble(evt);
    // Attempt to delay this until the third open has been processed;
    // not strictly necessary but will exercise IPC/event timing.
    setTimeout(function() {
        evalAndLog("db.close()");
    }, 0);
}

function connection2UpgradeNeeded(evt)
{
    preamble(evt);
    shouldBe("event.oldVersion", "1");
    shouldBe("event.newVersion", "2");
}

function connection2OpenSuccess(evt)
{
    preamble(evt);
    evalAndLog("db2 = event.target.result");
    shouldBe("db2.version", "2");
}

function connection3Blocked(evt)
{
    preamble(evt);
    evalAndLog("db2.close()");
}

function connection3UpgradeNeeded(evt)
{
    preamble(evt);
    shouldBe("event.oldVersion", "2");
    shouldBe("event.newVersion", "3");
}

function connection3OpenSuccess(evt)
{
    preamble(evt);
    evalAndLog("db3 = event.target.result");
    shouldBe("db3.version", "3");
    finishJSTest();
}
