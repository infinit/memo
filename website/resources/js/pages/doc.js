$(document).ready(function() {

  $('pre code').each(function(i, block) {
    hljs.highlightBlock(block);
  });

  $('a[href*=#]:not([href=#])').click(function() {
    if (location.pathname.replace(/^\//,'') === this.pathname.replace(/^\//,'') && location.hostname === this.hostname) {
      var target = $(this.hash);
      target = target.length ? target : $('[name=' + this.hash.slice(1) +']');
      if (target.length) {
        $('html,body').animate({
          scrollTop: target.offset().top
        }, 500);
        return false;
      }
    }
  });

  if ($('body').hasClass('documentation')) {

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
  }


  if ($('body').hasClass('doc_get_started')) {

    var winHeight = $(window).height(),
        docHeight = $(document).height(),
        progressBar = $('progress'),
        max, value;

    max = docHeight - winHeight - $("footer").height();
    progressBar.attr('max', max);

    $(document).on('scroll', function(){
       value = $(window).scrollTop();
       progressBar.attr('value', value);
    });
  }

});