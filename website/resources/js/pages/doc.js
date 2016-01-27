$(document).ready(function() {

  $('pre code').each(function(i, block) {
    hljs.configure({ languages: ['bash', 'cpp'] });
    hljs.initHighlighting();
  });

  if (!$('body').hasClass('doc_deployments')) {
    $('a[href*=#]:not([href=#])').click(function() {
      if (location.pathname.replace(/^\//,'') === this.pathname.replace(/^\//,'') && location.hostname === this.hostname) {
        var target = $(this.hash);
        target = target.length ? target : $('[name=' + this.hash.slice(1) +']');
        if (target.length) {
          $('html,body').animate({
            scrollTop: target.offset().top - 30
          }, 500);
          return false;
        }
      }
    });
  }


  if ($('body').hasClass('documentation') || $('body').hasClass('opensource')) {

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


  if ($('body').hasClass('doc_get_started') ) {
    $('a.button').click(function() {
      ga('send', 'event', 'download', $(this).text(), navigator.userAgent);
    });

    var winHeight = $(window).height(),
        docHeight = $(document).height(),
        progressBar = $('progress'),
        tooltip = $('#progressTooltip'),
        max, value, porcent;

    max = docHeight - winHeight - $("footer").height();
    progressBar.attr('max', max);

    $(document).on('scroll', function(){
       value = $(window).scrollTop();
       porcent = value / max * 100;

      if (porcent > 1 && porcent < 96) {
        tooltip.text(Math.round(porcent) + '%');
        tooltip.css('left', (porcent - 1) + '%');
        tooltip.show();
      }

      if (porcent > 96) {
        tooltip.text('👍');
      } else {
        progressBar.attr('value', value);
      }

      if (porcent < 1) {
        tooltip.hide();
      }
    });
  }

  if ($('body').hasClass('doc_deployments')) {
    tabby.init();
  }

});