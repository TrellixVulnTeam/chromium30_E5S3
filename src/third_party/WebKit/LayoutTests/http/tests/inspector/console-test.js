var initialize_ConsoleTest = function() {

InspectorTest.showConsolePanel = function()
{
    WebInspector.showPanel("console");
    WebInspector.drawer.immediatelyFinishAnimation();
}

InspectorTest.dumpConsoleMessages = function(printOriginatingCommand, dumpClassNames)
{
    var result = [];
    var visibleMessagesIndices = WebInspector.consoleView._visibleMessagesIndices;
    for (var i = 0; i < visibleMessagesIndices.length; ++i) {
        var message = WebInspector.console.messages[visibleMessagesIndices[i]];
        var element = message.toMessageElement();

        if (dumpClassNames) {
            var classNames = [];
            for (var node = element.firstChild; node; node = node.traverseNextNode(element)) {
                if (node.nodeType === Node.ELEMENT_NODE && node.className)
                    classNames.push(node.className);
            }
        }

        InspectorTest.addResult(element.textContent.replace(/\u200b/g, "") + (dumpClassNames ? " " + classNames.join(" > ") : ""));
        if (printOriginatingCommand && message.originatingCommand) {
            var originatingElement = message.originatingCommand.toMessageElement();
            InspectorTest.addResult("Originating from: " + originatingElement.textContent.replace(/\u200b/g, ""));
        }
    }
    return result;
}

InspectorTest.dumpConsoleMessagesWithStyles = function(sortMessages)
{
    var result = [];
    var indices = WebInspector.consoleView._visibleMessagesIndices;
    for (var i = 0; i < indices.length; ++i) {
        var element = WebInspector.console.messages[indices[i]].toMessageElement();
        InspectorTest.addResult(element.textContent.replace(/\u200b/g, ""));
        var spans = element.querySelectorAll(".console-message-text > span > span");
        for (var j = 0; j < spans.length; j++)
            InspectorTest.addResult("Styled text #" + j + ": " + (spans[j].style.cssText || "NO STYLES DEFINED"));
    }
}

InspectorTest.dumpConsoleMessagesWithClasses = function(sortMessages) {
    var result = [];
    var indices = WebInspector.consoleView._visibleMessagesIndices;
    for (var i = 0; i < indices.length; ++i) {
        var element = WebInspector.console.messages[indices[i]].toMessageElement();
        result.push(element.textContent.replace(/\u200b/g, "") + " " + element.getAttribute("class"));
    }
    if (sortMessages)
        result.sort();
    for (var i = 0; i < result.length; ++i)
        InspectorTest.addResult(result[i]);
}

InspectorTest.expandConsoleMessages = function()
{
    var indices = WebInspector.consoleView._visibleMessagesIndices;
    for (var i = 0; i < indices.length; ++i) {
        var element = WebInspector.console.messages[indices[i]].toMessageElement();
        var node = element;
        while (node) {
            if (node.treeElementForTest)
                node.treeElementForTest.expand();
            node = node.traverseNextNode(element);
        }
    }
}

InspectorTest.checkConsoleMessagesDontHaveParameters = function()
{
    var indices = WebInspector.consoleView._visibleMessagesIndices;
    for (var i = 0; i < indices.length; ++i) {
        var m = WebInspector.console.messages[indices[i]];
        InspectorTest.addResult("Message[" + i + "]:");
        InspectorTest.addResult("Message: " + WebInspector.displayNameForURL(m.url) + ":" + m.line + " " + m.message);
        if ("_parameters" in m) {
            if (m._parameters)
                InspectorTest.addResult("FAILED: message parameters list is not empty: " + m._parameters);
            else
                InspectorTest.addResult("SUCCESS: message parameters list is empty. ");
        } else {
            InspectorTest.addResult("FAILED: didn't find _parameters field in the message.");
        }
    }
}

InspectorTest.waitUntilMessageReceived = function(callback)
{
    InspectorTest.addSniffer(WebInspector.console, "addMessage", callback, false);
}

}
