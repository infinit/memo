$(document).ready(function() {

  $('pre code').each(function(i, block) {
    hljs.highlightBlock(block);
  });

  var a = function () {
    var height = $(window).scrollTop();
    var menu_anchor = $("#menu-anchor").offset().top - 13;
    var footer = $("footer").offset().top;
    var menu = $("ul.menu");
    var menu_height = $("ul.menu").height() + 60; // margin bottom

    if (height > menu_anchor) {
      var myTop = $(window).scrollTop();

      if (myTop > footer - menu_height) {
        myTop = footer - menu_height;
        menu.css({
          position: "absolute",
          top: myTop,
          bottom: ""
        });

      } else {
        menu.css({
          position: "fixed",
          top: '14px',
          bottom: ""
        });
      }

    } else {
      menu.css({
        position: "absolute",
        top: "",
        bottom: ""
      });
    }
  };

  $(window).scroll(a);

});