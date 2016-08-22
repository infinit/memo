$(document).ready(function() {

  if ($('body').hasClass('home')) {

    $(window).scroll(function () {
      if ($(window).scrollTop() > 400) {
        $('.btn-contact').addClass('show');
      }
    });

    if (window.location.hash === '#slack') {
      $.magnificPopup.open({
        items: { src: '#slack'},
        type: 'inline'
      }, 0);

      $('#slack').show();
    }

    var client = algoliasearch("2BTFITEL0N", "f27772a74737b4bb66eb0999104830d2");
    var index = client.initIndex('infinit_sh_faq');
    autocomplete('#search', { hint: false }, [
      {
        source: autocomplete.sources.hits(index, { hitsPerPage: 5 }),
        displayKey: 'question',
        templates: {
          suggestion: function(suggestion) {
            return suggestion._highlightResult.question.value;
          }
        }
      }
    ]).on('autocomplete:selected', function(event, suggestion, dataset) {
      var query = $('#search').val();
      window.location.href = "/faq?q=" + query;
    });
  }

});
