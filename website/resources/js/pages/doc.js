function showPopupMenu(element) {
  $(element).parent().toggleClass('clicked');
  $('#full').fadeIn('fast');
  $li = $('li.comparisons');
  $popup = $('li.comparisons ul');

  $(document).keyup(function(e) {
     if (e.keyCode === 27) {
      $li.removeClass('clicked');
      $('#full').fadeOut('fast');
    }
  });

  $(window).on("click", function(event) {
    if ($li.has(event.target).length === 0 && !$li.is(event.target)) {
      $li.removeClass('clicked');
      $('#full').fadeOut('fast');
      $(document).unbind('keyup');
    }
  });
}

$(document).ready(function() {

  /*----------------.
  | All             |
  `----------------*/

  if ($('body').hasClass('documentation')) {
    // dropdown menus
    $('.side-menu ul.tier1 li.dropdown > a').click(function(e) {
      e.preventDefault();
      $(this).parent().toggleClass('clicked');
    });

    // comparisons menu
    $('.side-menu ul.tier1 li.comparisons > a').click(function(e) {
      showPopupMenu(this);
      e.preventDefault();
    });

    if (window.location.hash === '#comparisons') {
      $('#full').fadeIn();
      showPopupMenu($('ul.menu li.comparisons > a'));
    }
  }

  if ($('body').hasClass('documentation')) {
    var menu;

    if ($('body').attr('class').indexOf("doc_kv") >= 0) {
      menu = $("#page-menu ul.tier1");
    } else {
      menu = $(".side-menu ul.tier1");
    }

    var a = function () {
      var height = $(window).scrollTop();
      var menu_anchor = $("#menu-anchor").offset().top - 13;
      var footer = $("footer").offset().top;
      
      var menu_height = $(".side-menu ul.tier1").height() + 60; // margin bottom

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
  | Product         |
  `----------------*/

  if ($('body').hasClass('product') || $('body').hasClass('docker')) {

    $('#play').click(function(e) {
      e.preventDefault();
      $('.schema').addClass('play');
      $('#play').hide();
      $('#replay').show();
    });

    $("#replay").click(function(e) {
      e.preventDefault();
      var el = $('.schema');
      var newone = el.clone(true);

      el.before(newone);
      $(".schema" + ":last").remove();
    });
  }



  /*----------------.
  | Reference       |
  `----------------*/

  if ($('body').hasClass('opensource')) {
    // Gets the full repo name from the data attribute and
    // fetches the data from the api.
    $('[data-gh-project]').each(function() {
      var $proj = $(this);
      var repo = $proj.data('gh-project');
      $.get('https://api.github.com/repos/' + repo).success(function(data) {
        //$proj.find('.star span').text(data.forks_count);
        $proj.find('.star span').text(data.stargazers_count);

        $proj.find('.stats .stars span').text(data.stargazers_count);
        $proj.find('.stats .forks span').text(data.forks);
        $proj.find('.stats .issues span').text(data.open_issues);
        $proj.find('.date span').text(moment(data.pushed_at).fromNow());
      });
    });
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

  if ($('body').hasClass('doc_technology')) {
    // $('.open-popup').magnificPopup({
    //   type:'inline',
    //   midClick: true,
    //   mainClass: 'mfp-fade'
    // });

    // $('.open-popup').click(function() {
    //   $('#iam-policy').show();
    // });
  }


  if ($('body').hasClass('doc_reference') || $('body').hasClass('doc_deployments') || $('body').hasClass('doc_get_started') || $('body').attr('class').indexOf("doc_kv") >= 0) {
    var enableSubMenu = function () {
      var position = $(window).scrollTop() + 100;
      var anchors, targets;

      if ($('body').hasClass('doc_get_started') || $('body').attr('class').indexOf("doc_kv") >= 0) {
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
            targets = '.side-menu ul.tier1 li';
          } else if ($('body').attr('class').indexOf("doc_kv") >= 0) {
            targets = '#page-menu ul.tier1 li';
          } else {
            targets = '.side-menu ul.tier1 li.scroll_menu ul li';
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

  function showInstallProcedure(platform) {

    // Reset Debian tabs choice package/tarball 
    if (platform !== 'debian') {
      $('.tabs-pane').addClass('active');
    } else {
      $('.tabs-pane').removeClass('active');
      $('.tabs-pane#linux-repository-install').addClass('active');
    }

    // Toggle platform instructions display throughout the guide
    $('[data-platform]').not('[data-os]').hide();
    $('[data-platform~="' + platform + '"]').not('[data-os]').show();
  }

  if ($('body').hasClass('doc_get_started') ) {
    $('a.button').click(function() {
      ga('send', 'event', 'download', $(this).text(), navigator.userAgent);
    });

    // Enable platform tabs
    $('.tabs-circle a').click(function(e) {
      $('.tabs-circle li').removeClass('active');
      $(this).parent().addClass('active');

      var platform = $(this).parent().attr('data-platform');

      showInstallProcedure(platform);
      e.preventDefault();
    });

    // Initiate platform instructions when not Windows
    if (!$('#get_started').hasClass('windows')) {
      showInstallProcedure($('.tabs-circle li.active').attr('data-platform'));
    }

    // var winHeight = $(window).height(),
    //     docHeight = $(document).height(),
    //     progressBar = $('progress'),
    //     tooltip = $('#progressTooltip'),
    //     max, value, porcent;

    // max = docHeight - winHeight - ($("footer").height() + $('.next').height());
    // progressBar.attr('max', max);
  }

  if ($('body').hasClass('doc_deployments') || $('body').hasClass('doc_get_started') || $('body').hasClass('doc_storages_s3')) {
    tabby.init();
  }

  /*----------------.
  | KV              |
  `----------------*/

  function mergeAllSnippets(language) {
    var fullCode;
    var pre_elements = $('pre code.lang-' + language);

    if (language === 'cpp') { pre_elements = 'pre code.cpp'; }

    $(pre_elements).not('pre code.notInFullCode').each(function(index, obj) {
      if (index === 0) { 
        fullCode = $(this).text();
      } else {
        fullCode += $(this).text() + '\r';
      }
    });
    return fullCode;
  }

  if ($('body').hasClass('doc_kv')) {

    // Display Go snippets by default
    $('code.cpp').parent().not('pre.goal').hide();
    $('code.lang-python').parent().hide();

    // Switch language
    $('a[data-language]').click(function(e) {
      var elementstoShow;
      var language = $(this).attr('data-language');

      if (language === 'cpp') {
        elementstoShow = $('code.' + language);
      } else {
        elementstoShow = $('code.lang-' + language);
      }

      $('pre').not('pre.goal').hide();
      elementstoShow.parent().show();

      $('a[data-language]').removeClass('active');
      $('a[data-language=' + language + ']').addClass('active');

      e.preventDefault();
    });

    // Merge all snippets of the page
    // While excluding generic ones
    $('pre code.complete.lang-go').text(mergeAllSnippets('go'));
    $('pre code.complete.lang-python').text(mergeAllSnippets('python'));
    $('pre code.complete.cpp').text(mergeAllSnippets('cpp'));

    // Clone language bar before all the snippets
    $('code.lang-python').parent().before($('ul.switchLanguage'));

    $.ajax({
      url : '/scripts/kv/doughnut.proto',
      dataType: "text",
      success : function (data) {
        $("#doughnut-proto pre code").text(data);
        $('.popup pre code').each(function(i, block) {
          hljs.highlightBlock(block);
        });
      }
    });

    // Refresh highlight.js
    $('pre code').each(function(i, block) {
      hljs.highlightBlock(block);
    });
  }

});