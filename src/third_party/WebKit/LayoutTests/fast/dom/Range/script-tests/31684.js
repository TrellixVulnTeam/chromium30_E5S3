description(
    "This test checks an orphan text node cannot be surrounded by the range. (bug31684)"
);

var range = document.createRange();
var text = document.createTextNode('hello');
var element = document.createElement("div");
range.selectNodeContents(text);

shouldThrow("range.surroundContents(element)", '"HierarchyRequestError: A Node was inserted somewhere it doesn\'t belong."');
