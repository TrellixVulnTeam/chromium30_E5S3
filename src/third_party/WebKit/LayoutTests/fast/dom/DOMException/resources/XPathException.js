description("Tests the properties of the exception thrown when using XPath.")

var e;
try {
    var evalulator = new XPathEvaluator;
    var nsResolver = evalulator.createNSResolver(document);
    var result = evalulator.evaluate("/body", document, nsResolver, 0, null);
    var num = result.numberValue;
   // raises a TYPE_ERR
} catch (err) {
    e = err;
}

shouldBeEqualToString("e.toString()", "TypeError: Type error");
shouldBeEqualToString("Object.prototype.toString.call(e)", "[object Error]");
shouldBeEqualToString("Object.prototype.toString.call(e.__proto__)", "[object Error]");
shouldBeEqualToString("e.constructor.toString()", "function TypeError() { [native code] }");
shouldBe("e.constructor", "window.TypeError");
