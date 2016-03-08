function showPopupMenu(element) {
  $(element).parent().toggleClass('clicked');
  $('#full').fadeIn('fast');
  $li = $('li.comparisons');
  $popup = $('li.comparisons ul');

  $(window).on("click", function(event){
    if ($li.has(event.target).length === 0 && !$li.is(event.target)) {
      $li.removeClass('clicked');
      $('#full').fadeOut('fast');
    }
  });
}

$(document).ready(function() {

  $('pre code').each(function(i, block) {
    hljs.configure({ languages: ['bash', 'cpp'] });
    hljs.initHighlighting();
  });

  $('a[href*=#]:not([href=#]):not([data-tab])').click(function() {
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

  /*----------------.
  | All             |
  `----------------*/

  if ($('body').hasClass('documentation')) {
    // dropdown menus
    $('ul.menu li.dropdown > a').click(function(e) {
      e.preventDefault();
      $(this).parent().toggleClass('clicked');
    });

    // comparisons menu
    $('ul.menu li.comparisons > a').click(function(e) {
      showPopupMenu(this);
      e.preventDefault();
    });

    if (window.location.hash === '#comparisons') {
      $('#full').fadeIn();
      showPopupMenu($('ul.menu li.comparisons > a'));
    }
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
            'z-index': '12',
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

  /*----------------.
  | Reference       |
  `----------------*/

  if ($('body').hasClass('doc_reference')) {
    $('.iam_policy').magnificPopup({
      type:'inline',
      midClick: true,
      mainClass: 'mfp-fade'
    });

    $('.iam_policy').click(function() {
      $('#iam-policy').show();
    });
  }


  if ($('body').hasClass('doc_reference') || $('body').hasClass('doc_deployments') || $('body').hasClass('doc_get_started')) {
    var enableSubMenu = function () {
      var position = $(window).scrollTop() + 100;
      var anchors, targets;

      if ($('body').hasClass('doc_get_started')) {
        anchors = $('h2, h3');
      } else {
        anchors = $('h2');
      }

      $(anchors).each(function(i, anchor) {
        if
        (
          (anchors[i+1] !== undefined &&
          position > $(anchor).offset().top &&
          position < $(anchors[i+1]).offset().top) || (
          anchors[i+1] === undefined &&
          position > $(anchor).offset().top)
        )
        {
          if ($('body').hasClass('doc_get_started')) {
            targets = 'ul.menu li';
          } else {
            targets = 'ul.menu li.scroll_menu ul li';
          }

          if (!$(anchor).hasClass('skip')) {
            $(targets).removeClass('active');
            $(targets + '.' + $(anchor).attr('id')).addClass('active');
            return false;
          }
        }
      });
    };

    $(window).scroll(enableSubMenu);
  }

  /*----------------.
  | Get Started     |
  `----------------*/

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

  if ($('body').hasClass('doc_deployments') || $('body').hasClass('doc_changelog')) {
    tabby.init();
  }


  /*----------------.
  | Comparisons     |
  `----------------*/

  function displayComparison() {
    $('.properties').toggleClass('compared');
    if (elem.checked) {
      $('.property .infinit').show();
    } else {
      $('.property .infinit').hide();
    }
  }

  if ($('body').hasClass('doc_comparison')) {
    var elem = document.querySelector('.js-switch');
    var switcher = new Switchery(elem, { color: "#252d3b"});

    elem.onchange = function() {
      displayComparison();
    };

    $('.open-popup').magnificPopup({
      type:'inline',
      midClick: true,
      mainClass: 'mfp-fade'
    });

    $('.open-popup').click(function() {
      $($(this).attr('href')).show();
    });

    if (window.location.hash === '#slack') {
      $.magnificPopup.open({
        items: { src: '#slack'},
        type: 'inline'
      }, 0);

      $('#slack').show();
    }

    displayComparison();
  }

});